#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <assert.h>

#include "utils.h"
#include "http_parser/http_parser.h"
#include "stream_buffer.h"
#include "epoll_interface.h"
#include "proxy_state_machine.h"

#define LISTENING_PORT 8000
#define MAX_EPOLL_EVENTS 100

int main(int argc, char *argv[])
{
    int listener_fd = -1;

    // Setup Listening socket.
    listener_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        log_error("Error while opening listening socket");
        exit(EXIT_FAILURE);
    }

    // Bind socket to a port.
    struct sockaddr_in sockaddrin;
    sockaddrin.sin_addr.s_addr = INADDR_ANY;
    sockaddrin.sin_family = PF_INET;
    sockaddrin.sin_port = htons(LISTENING_PORT);
    if (bind(listener_fd, (struct sockaddr *)&sockaddrin, sizeof(sockaddrin)) != 0) {
        log_error("Error while binding to the port.");
        exit(EXIT_FAILURE);
    }

    // Make socket non-blocking.
    if (make_socket_non_blocking(listener_fd) < 0) {
        exit(EXIT_FAILURE);
    }

    // Set listener on the socket for incoming connections.
    if (listen(listener_fd, 100) != 0) {
        log_error("Error while setting up listener on the socket.");
        exit(EXIT_FAILURE);
    }

    // Setup epoll.
    int epfd = -1;
    struct epoll_event event;
    struct epoll_event *events;
    epfd = epoll_create1(0);
    if (epfd < 0) {
        log_error("Error while creating epoll fd");
        exit(EXIT_FAILURE);
    }

    // Setup epoll on a listening socket.
    struct sock_context *scxt = malloc(sizeof(struct sock_context));
    if (scxt == NULL) {
        exit(EXIT_FAILURE);
    }
    scxt->fd = listener_fd;
    scxt->cxt = NULL;
    event.data.ptr = scxt;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listener_fd, &event) != 0) {
        log_error("Error while setting up epoll on a listener socket");
        exit(EXIT_FAILURE);
    }



    // Ignore SIGPIPE. Errors due to client/upstream abrupt disconnects is handled
    // in the event loop.
    signal(SIGPIPE, SIG_IGN);

    events = malloc(MAX_EPOLL_EVENTS * sizeof(struct epoll_event));
    memset(events, 0, sizeof(struct epoll_event) * MAX_EPOLL_EVENTS);
    while(1) {
        int n = 0;
        n = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, -1);
        for(int i = 0; i < n; i++) {
            server_epoll_event(listener_fd, epfd, &events[i]);
        }
    }

    free(scxt);
    free(events);
    exit(EXIT_SUCCESS);
}
