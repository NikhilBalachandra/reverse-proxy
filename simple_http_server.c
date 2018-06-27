#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>

#define LISTENING_PORT 8001

int main(int argc, char *argv[])
{
    int ret = -1;

    // Create socket
    int listening_fd = -1;
    listening_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listening_fd < 0)
    {
        fprintf(stderr, "Error: \"%s\" while creating socket.", strerror(errno));
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Bind
    struct sockaddr_in sockaddrin;
    sockaddrin.sin_addr.s_addr = INADDR_ANY;
    sockaddrin.sin_family = PF_INET;
    sockaddrin.sin_port = htons(LISTENING_PORT);
    ret = bind(listening_fd, (struct sockaddr *)&sockaddrin, sizeof(sockaddrin));
    if (ret < 0)
    {
        fprintf(stderr, "Error: \"%s\" while binding to the port %d", strerror(errno), LISTENING_PORT);
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connection.
    if (listen(listening_fd, 128) != 0)
    {
        fprintf(stderr, "Error: \"%s\" while listen() call.", strerror(errno));
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    int connection_fd = -1;
    struct sockaddr_in clientaddr;
    uint clientaddr_size = sizeof(clientaddr);
    char *buf[100];
    char *response_headers = "HTTP/1.0 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %d\r\n\
Connection: close\r\n\
\r\n";
    char *response_body = "<!DOCTYPE html><html><head><title>World's best title</title></head><body><h1>Hello from Server</h1></body</html>";

    char *response   = malloc(((strlen(response_headers) + strlen(response_body)) + floor(log10(strlen(response_body)))+1-2) * sizeof(char));
    char *response1   = malloc(((strlen(response_headers) + strlen(response_body)) + floor(log10(strlen(response_body)))+1-2) * sizeof(char));
    strcat(response1, response_headers);
    strcat(response1, response_body);
    sprintf(response, response1, strlen(response_body));
    while(1)
    {
        printf("Ready to accept new connection\n");
        connection_fd = accept(listening_fd, (struct sockaddr *)&clientaddr, &clientaddr_size);
        printf("Connected\n");
        ulong transmitted = 0;
        while (transmitted != strlen(response)) {
            int i = send(connection_fd, response, strlen(response), 0);
            if (i < 0) {
                fprintf(stderr, "No more data to send.\n");
                fflush(stderr);
                break;
            }
            transmitted += i;
        }
        printf("Send all the data\n");
        while (recv(connection_fd, buf, 100, 0) > 0);
        printf("Read to end\n");
        shutdown(connection_fd, SHUT_RDWR);
        close(connection_fd);
    }

    // free(response);
    exit(EXIT_SUCCESS);
}
