/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include "net.h"
#include "file.h"
#include "mime.h"
#include "cache.h"

#define PORT "3490" // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body, int content_length)
{
    const int max_response_size = 65536 * 10;
    char response[max_response_size];

    // Build HTTP response and store it in response

    // get the time for the response, type time_t
    time_t cur_time = time(NULL);
    // convert to struct tm (asctime converts to string)
    struct tm *time_tm = localtime(&cur_time);

    // sprintf returns length of str
    int response_length = sprintf(response,
                                  "%s\n" // header
                                  "Date: %sConnection: close\n"
                                  "Content-Type: %s\nContent-Length: %d\n"
                                  "\n", // end marker for header
                                        //   "%s\n", // body
                                  header, asctime(time_tm), content_type,
                                  content_length);
    memcpy(response + response_length, body, content_length);
    // Send it all!
    int rv = send(fd, response, response_length + content_length, 0);

    if (rv < 0)
    {
        perror("send");
    }

    return rv;
}

/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd)
{
    // Generate a random number between 1 and 20 inclusive
    srand(time(NULL));
    int d20;
    d20 = rand() % 20 + 1;

    // set up the "body" of the response
    char body[16];
    sprintf(body, "%d", d20);

    // Use send_response() to send it back as text/plain data
    send_response(fd, "HTTP/1.1 200 OK", "text/plain", body, strlen(body));
}

/**
 * Send a 404 response
 */
void resp_404(int fd)
{
    char filepath[4096];
    struct file_data *filedata;
    char *mime_type;

    // Fetch the 404.html file
    snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
    filedata = file_load(filepath);

    if (filedata == NULL)
    {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system 404 file\n");
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * Read and return a file from disk or cache
 */
void get_file(int fd, struct cache *cache, char *request_path)
{
    char filepath[4096];
    struct file_data *filedata;
    char *mime_type;

    // load the filepath variable
    sprintf(filepath, "./serverroot%s", request_path);

    // When a file is requested, first check to see if the path to the file is in the cache(use the file path as the key).
    struct cache_entry *cache_entry = cache_get(cache, filepath);

    //   If it's there, serve it back.
    if (cache_entry != NULL)
    {
        send_response(fd, "HTTP/1.1 200 OK", cache_entry->content_type, cache_entry->content, cache_entry->content_length);
    }
    //   If it's not there:
    else
    {
        // Fetch the file
        filedata = file_load(filepath);

        if (filedata == NULL)
        {
            // if file not found, look to see if there's an index.html in the dir
            sprintf(filepath, "./serverroot/%s%s", request_path, "/index.html");
            filedata = file_load(filepath);
            if (filedata == NULL)
            {
                resp_404(fd);
                return;
            }
        }

        mime_type = mime_type_get(filepath);

        // Store it in the cache
        cache_put(cache, filepath, mime_type, filedata->data, filedata->size);
        // serve the file
        send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data, filedata->size);

        file_free(filedata);
    }
}

/**
 * Search for the end of the HTTP header
 * 
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
char *find_start_of_body(char *header)
{
    // if (strstr(header, "\r\n\r\n") == 0)
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache)
{
    const int request_buffer_size = 65536; // 64K
    char request[request_buffer_size];
    char req_method[16]; // GET, POST, CONNECT, etc
    char req_uri[1024];
    char req_protocol[16]; // HTTP/1.1 or HTTP/2.0

    // Read request
    int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

    if (bytes_recvd < 0)
    {
        perror("recv");
        return;
    }

    // Read the three components of the first request line
    // printf("\nIN HANDLE_REQ\n\n%s\nend of request\n", request);
    sscanf(request, "%s %s %s", req_method, req_uri, req_protocol);

    // If GET, handle the get endpoints
    if (strcmp(req_method, "GET") == 0)
    {
        // Check if it's /d20 and handle that special case
        if (strcmp(req_uri, "/d20") == 0)
        {
            get_d20(fd);
        }
        else
        {
            // Otherwise serve the requested file by calling get_file()
            get_file(fd, cache, req_uri);
        }
    }
    // (Stretch) If POST, handle the post request
}

/**
 * Main
 */
int main(void)
{
    int newfd;                          // listen on sock_fd, new connection on newfd
    struct sockaddr_storage their_addr; // connector's address information
    char s[INET6_ADDRSTRLEN];

    struct cache *cache = cache_create(10, 0);

    // Get a listening socket
    int listenfd = get_listener_socket(PORT);

    if (listenfd < 0)
    {
        fprintf(stderr, "webserver: fatal error getting listening socket\n");
        exit(1);
    }

    printf("webserver: waiting for connections on port %s...\n", PORT);

    // This is the main loop that accepts incoming connections and
    // forks a handler process to take care of it. The main parent
    // process then goes back to waiting for new connections.

    while (1)
    {
        socklen_t sin_size = sizeof their_addr;

        // Parent process will block on the accept() call until someone
        // makes a new connection:
        newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (newfd == -1)
        {
            perror("accept");
            continue;
        }

        // Print out a message that we got the connection
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);

        // newfd is a new socket descriptor for the new connection.
        // listenfd is still listening for new connections.

        handle_http_request(newfd, cache);

        close(newfd);
    }

    // Unreachable code

    return 0;
}
