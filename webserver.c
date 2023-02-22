#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "config.h"
#include "helpers.h"

/*------------------------------------------------------------------------
* Program:   http server
*
* Purpose:   allocate a socket and then repeatedly execute the following:
*              (1) wait for the next connection from a client
*              (2) read http request, reply to http request
*              (3) close the connection
*              (4) go back to step (1)
*
* Syntax:    http_server [ port ]
*
*               port  - protocol port number to use
*
* Note:      The port argument is optional.  If no port is specified,
*            the server uses the port specified in config.h
*
*------------------------------------------------------------------------
*/

int main(int argc, char *argv[])
{
    /* structure to hold server's and client addresses, respectively */
    struct sockaddr_in server_address, client_address;

    int listen_socket = -1;
    int connection_socket = -1;
    int port = 0;

    /* id of child process to handle request */
    pid_t pid = 0;

    char response_buffer[MAX_HTTP_RESPONSE_SIZE] = "";
    int status_code = -1;
    char *status_phrase = "";

    /* 1) assign listen_socket */
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0)
    {
        perror("Error opening listen_socket");
        exit(1);
    }

    /** Check command-line argument for port and extract
     * port number if one is specified. Otherwise, use default
     */
    if (argc > 1)
    {
        /* Convert from string to integer */
        port = atoi(argv[1]);
    }
    else
    {
        port = DEFAULT_PORT;
    }

    if (port <= 0)
    {
        /* Test for legal value */
        fprintf(stderr, "bad port number %d\n", port);
        exit(EXIT_FAILURE);
    }

    /* Clear the server address */
    memset(&server_address, 0, sizeof(server_address));

    /* 2) Set the values for the server address structure */
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    /* 3) Bind the socket to the address information set in server_address */
    if ( bind( listen_socket, (struct sockaddr*)&server_address, sizeof(server_address) ) < 0 ) {
        perror("Error while binding");
        exit(1);
    }

    /* Start listening for connections */
    printf("Waiting connection on port %d...\n", port);
    if (listen(listen_socket, QLEN) < 0) {
        perror("Error while listening");
        exit(1);
    }

    /** Main server loop
     * Loop while the listen_socket is valid
     */
    while (listen_socket >= 0) 
    {
        /* Accept a connection */
        connection_socket = accept(listen_socket, (struct sockaddr*)&client_address, &pid );

        /* Fork a child process to handle this request */
        if ((pid = fork()) == 0)
        {
            /*----------START OF CHILD CODE----------------*/
            
            /* Close the listening socket */
            if (close(listen_socket) < 0) {
                fprintf(stderr, "child couldn't close listen socket\n");
                exit(EXIT_FAILURE);
            }

            /* See httpreq.h for definition */
            struct http_request new_request;

            /* if false then parse request failed | from helper.h */
            Parse_HTTP_Request(connection_socket, &new_request);

            /* Decide which status_code and status_phrase to return to client | GET and HEAD ok */
            bool is_ok_to_send_resource = false;
            if (strcmp(new_request.method, "GET")==0)
            {
                status_code = 200;
                is_ok_to_send_resource = true;
            }
            else if (strcmp(new_request.method, "HEAD")==0)
                status_code = 200;
            
            else if (   
                        strcmp(new_request.method, "OPTIONS") == 0    ||
                        strcmp(new_request.method, "CONNECT") == 0    ||
                        strcmp(new_request.method, "DELETE") == 0     ||
                        strcmp(new_request.method, "TRACE") == 0      ||
                        strcmp(new_request.method, "PATCH") == 0      ||
                        strcmp(new_request.method, "POST") == 0       ||
                        strcmp(new_request.method, "PUT") == 0        
                    )
                status_code = 501;
            else status_code = 400;
            
            if (status_code == 400)
                status_phrase = "Bad Request";
            else if (status_code == 501)
                status_phrase = "Not Implemented";
            else if (status_code == 204)
                status_phrase = "No Content";
            else 
                status_phrase = "OK";
            
            if (status_code == 200 && !(Is_Valid_Resource(new_request.URI)))
            {
                status_code = 404;
                status_phrase = "Not Found";
            }
            
            /* Set the reply message to the client */
            sprintf(response_buffer, "HTTP/1.0 %d %s\r\n", status_code, status_phrase);

            printf("Sending response line: %s\n", response_buffer);

            /* Send the reply message to the client */
            send(connection_socket, response_buffer, strlen(response_buffer), 0);

            
            /** Send resource (if requested) under what condition will the
             * server send an entity body?
             */
            if (is_ok_to_send_resource) {
                printf("sending resource \n");
                Send_Resource(connection_socket, new_request.URI);
            }
            else {
                /* Do not send resource
                 * Send the HTTP headers
                 */
                if (strcmp(new_request.method, "HEAD")==0)
                    Send_Header(connection_socket, new_request.URI);
                
                else send(connection_socket, "\r\n\r\n", strlen("\r\n\r\n"), 0);
            }

            /* Child thread's work is done
             * Close remaining descriptors and exit 
             */
            if (connection_socket >= 0) {
                if (close(connection_socket) < 0) {
                    fprintf(stderr, "closing connected socket failed\n");
                    exit(EXIT_FAILURE);
                }
            }

            /* All done return to parent */
            exit(EXIT_SUCCESS);
        }
        /*----------END OF CHILD CODE----------------*/

        /** Back in parent process
         * Close parent's reference to connection socket,
         * then back to top of loop waiting for next request 
         */
        if (connection_socket >= 0) {
            if (close(connection_socket) < 0) {
                fprintf(stderr, "closing connected socket failed\n");
                exit(EXIT_FAILURE);
            }
        }

        /* if child exited, wait for resources to be released */
        waitpid(-1, NULL, WNOHANG);
    }
}

