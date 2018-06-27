#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "utils.h"
#include "proxy_state_machine.h"
#include "epoll_interface.h"

// HTTP parser settings.
http_parser_settings PROXY_HTTP_PARSER_SETTINGS = {
    .on_message_complete = on_message_complete,
    .on_message_begin = 0,
    .on_header_field = 0,
    .on_header_value = 0,
    .on_url = 0,
    .on_status = 0,
    .on_body = 0,
    .on_headers_complete = 0,
    .on_chunk_header = 0,
    .on_chunk_complete = 0,
};

int make_socket_non_blocking(int socket)
{
    int flags = 0;
    flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        log_error("Error getting socket flags");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(socket, F_SETFL, flags) == -1) {
        log_error("Error while setting O_NONBLOCK option on the socket");
        return -1;
    }
    return 0;
}

static int connect_upstream(int *fd)
{
    *fd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr_in;
    addr_in.sin_family = PF_INET;
    addr_in.sin_port = htons(UPSTREAM_PORT);
    inet_pton(PF_INET, UPSTREAM_HOST, &addr_in.sin_addr.s_addr);
    make_socket_non_blocking(*fd);
    return connect(*fd, (struct sockaddr *)&addr_in, sizeof(addr_in));
}

static inline int server_add_socket_to_epoll(int epfd, int fd, struct proxy_context *cxt)
{
    struct epoll_event event;
    struct sock_context *scxt;

    if ((scxt = malloc(sizeof(struct sock_context))) == NULL) {
        return -1;
    }

    if (cxt == NULL) {
        if ((cxt = calloc(1, sizeof(struct proxy_context))) == NULL) {
            free(scxt);
            return -1;
        }
        if (proxy_context_init(cxt) < 0) {
            free(scxt);
            free(cxt);
            return -1;
        }
        proxy_context_set_client(cxt, fd);
    } else {
        proxy_context_set_upstream(cxt, fd);
    }

    scxt->fd = fd;
    scxt->cxt = cxt;
    event.events = EPOLLET | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
    event.data.ptr = scxt;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

static inline int server_del_socket_from_epoll(int epfd, struct sock_context *scxt)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, scxt->fd, NULL);
    // epoll_ctl(epfd, EPOLL_CTL_DEL, scxt->cxt->upstream_fd, NULL);
    // close(scxt->cxt->client_fd);
    // close(scxt->cxt->upstream_fd);
}

static inline void server_free_context(struct sock_context *scxt)
{
    proxy_context_free(scxt->cxt);
    free(scxt->cxt);
    free(scxt);
}

int server_epoll_event(const int listener_fd, const int epfd, const struct epoll_event *event)
{
    struct sock_context *scxt = (struct sock_context *) event->data.ptr;
    struct proxy_context *cxt = scxt->cxt;
    int fd = scxt->fd;

    if (cxt) {
        printf("Event: fd: %d, client-fd: %d, upstream-fd %d\n", fd, cxt->client_fd, cxt->upstream_fd);
    }

    if (event->events & EPOLLERR) {
        printf("Received EPOLLERR\n");
        server_del_socket_from_epoll(epfd, scxt);
        close(cxt->upstream_fd);
        close(cxt->client_fd);
        server_free_context(scxt);
        return 0;
    }

    if (event->events & EPOLLHUP) {
        printf("Received EPOLLHUP\n");
        server_del_socket_from_epoll(epfd, scxt);
        close(cxt->upstream_fd);
        close(cxt->client_fd);
        server_free_context(scxt);
        return 0;
    }

    if (event->events & EPOLLRDHUP) {
        printf("Received EPOLLRDHUP\n");
        // shutdown(cxt->upstream_fd, SHUT_W);
        // shutdown(cxt->client_fd, SHUT_W);
    }

    // New client(s) waiting to connected. Accept them and to event loop.
    if ((event->events & EPOLLIN) && (fd == listener_fd)) {
        int client_fd = -1;
        while(1) {
            client_fd = accept(listener_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
            }
            if (make_socket_non_blocking(client_fd) < 0) {
                close(client_fd);
                continue;
            }
            if (server_add_socket_to_epoll(epfd, client_fd, NULL) < 0) {
                if (errno == ENOSPC) {
                    // limit imposed by /proc/sys/fs/epoll/max_user_watches was encountered.
                    // LOG useful error for the user telling to increase limit.
                } else {
                    // LOG: Something fatal.
                }
                close(client_fd); // Better to respond with 503 Service Unavailable.
            }
        }
        return 0;
    }

    if (event->events & EPOLLIN) {
        printf("EI.C:cxt->upstream %d, cxt->client %d, fd %d, phase %d\n", cxt->upstream_fd, cxt->client_fd, fd, cxt->phase);
        proxy_context_transit_state(cxt, fd);

        // TODO: Replace this assertion with if check and closure.
        if(!(cxt->phase == PROXY_S_CLIENT_REQUEST_RECEIVING ||
             cxt->phase == PROXY_S_UPSTREAM_RESPONSE_RECEIVING)) {
             server_del_socket_from_epoll(epfd, scxt);
             close(cxt->upstream_fd);
             close(cxt->client_fd);
             server_free_context(scxt);
             return 0;
        }

        // TODO: Also assert parser type (HTTP_REQUEST or HTTP_RESPONSE) with phase.

        int total_recvd = 0;
        char *buf = &cxt->buffer->buf[cxt->buffer->size];
        printf("EI.C:cxt->upstream %d, cxt->client %d, fd %d, phase %d\n", cxt->upstream_fd, cxt->client_fd, fd, cxt->phase);
        while (1) {
            // Increase buffer size if required.
            if (cxt->buffer->size == cxt->buffer->capacity) {
                printf("Trying to resize buffer\n");
                if (stream_buffer_t_resize(cxt->buffer, 0) < 0) {
                    printf("Error resizing buffer\n");
                    server_del_socket_from_epoll(epfd, scxt);
                    close(cxt->upstream_fd);
                    close(cxt->client_fd);
                    server_free_context(scxt);
                    break;
                }
            }

            char *buff = &cxt->buffer->buf[cxt->buffer->size];
            uint buf_len = cxt->buffer->capacity - cxt->buffer->size;
            int recvd = recv(fd, buff, buf_len, 0);
            printf("Buffer length %d, size: %d, capacity %d, cursor %d\n",
                    buf_len,
                    cxt->buffer->size,
                    cxt->buffer->capacity,
                    cxt->buffer_cursor);
            if (recvd < 0) {
                if (errno != EAGAIN || errno != EWOULDBLOCK) {
                    printf("Error in the socket recv\n");
                    server_del_socket_from_epoll(epfd, scxt);
                    close(cxt->upstream_fd);
                    close(cxt->client_fd);
                    server_free_context(scxt);
                    // TODO: LOG Error.
                }
                break;
            } else if (recvd == 0) {
                printf("Received 0 bytes from the socket. Closing\n");
                server_del_socket_from_epoll(epfd, scxt);
                close(fd);
                free(scxt);
                // close(cxt->upstream_fd);
                // close(cxt->client_fd);
                // server_free_context(scxt);
                break;
            }
            cxt->buffer->size += recvd;
            total_recvd += recvd;
        }
        printf("EI.C:cxt->upstream %d, cxt->client %d, fd %d, phase %d\n", cxt->upstream_fd, cxt->client_fd, fd, cxt->phase);
        printf("Outside EPOLLIN while\n");
        proxy_context_transit_state(cxt, fd);

        cxt->parser->data = cxt;
        int nparsed;
        nparsed = http_parser_execute(cxt->parser, &PROXY_HTTP_PARSER_SETTINGS, buf, total_recvd);

        if (nparsed != total_recvd) {
            server_del_socket_from_epoll(epfd, scxt);
            close(cxt->upstream_fd);
            close(cxt->client_fd);
            server_free_context(scxt);
        }
        if (fd == cxt->client_fd && cxt->phase == PROXY_S_CLIENT_REQUEST_RECEIVED) {
            proxy_context_switch_to_response(cxt);
            int upstream_fd = -1;
            int res = connect_upstream(&upstream_fd);
            if (make_socket_non_blocking(upstream_fd) < 0) {
                server_del_socket_from_epoll(epfd, scxt);
                close(cxt->upstream_fd);
                close(cxt->client_fd);
                server_free_context(scxt);
                res = -1; // HACK
            }
            if (res == 0) {
                cxt->upstream_fd = upstream_fd;
                printf("Connected to upstream\n");
                int sent = send(upstream_fd, &cxt->buffer->buf[cxt->buffer_cursor], cxt->buffer->size - cxt->buffer_cursor, 0);
                cxt->buffer_cursor += sent;
                // Next call would fail with EWOULDBLOCK.
                if (sent != (int)cxt->buffer->size) { // TODO: Fix lossy and dangerous typecast.
                    if (server_add_socket_to_epoll(epfd, upstream_fd, cxt) == -1) {
                        server_del_socket_from_epoll(epfd, scxt);
                        close(cxt->upstream_fd);
                        close(cxt->client_fd); // TODO: Respond to the client instead.
                        server_free_context(scxt);
                    }
                }
                cxt->phase = PROXY_S_UPSTREAM_REQUEST_SENDING;
            } else if (res < 0 && errno == EINPROGRESS) {
                cxt->upstream_fd = upstream_fd;
                printf("Connect to upstream in progress fd: %d\n", upstream_fd);
                if (server_add_socket_to_epoll(epfd, upstream_fd, cxt) == -1) {
                    // adding Upstream fd to epoll failed.
                    server_del_socket_from_epoll(epfd, scxt);
                    close(cxt->upstream_fd);
                    close(cxt->client_fd); // TODO: Respond to the client instead.
                    server_free_context(scxt);
                }
                cxt->phase = PROXY_S_UPSTREAM_CONNECTION_PROGRESS;
            } else {
                printf("Error connecting to upstream\n");
                server_del_socket_from_epoll(epfd, scxt);
                close(cxt->client_fd); // TODO: Respond to the client instead.
                server_free_context(scxt);
            }
        } else if (fd == cxt->upstream_fd && cxt->phase == PROXY_S_UPSTREAM_RESPONSE_RECEIVED) {
            shutdown(fd, SHUT_RD);
            close(fd);
            int sent = send(cxt->client_fd, &cxt->buffer->buf[cxt->buffer_cursor], cxt->buffer->size - cxt->buffer_cursor, 0);
            cxt->buffer_cursor += sent;
            cxt->phase = PROXY_S_CLIENT_RESPONSE_SENDING;
        }
    }

    if (event->events & EPOLLOUT) {
        if (!(cxt->phase == PROXY_S_UPSTREAM_CONNECTION_PROGRESS ||
            cxt->phase == PROXY_S_UPSTREAM_REQUEST_SENDING ||
            cxt->phase == PROXY_S_CLIENT_RESPONSE_SENDING)) {
            printf("Ignoring write event at phase %d for fd %d\n", cxt->phase, fd);
            return 0;
        }

        assert(fd == cxt->client_fd || fd == cxt->upstream_fd);

        if (fd == cxt->client_fd && (cxt->phase == PROXY_S_UPSTREAM_CONNECTION_PROGRESS ||
                                     cxt->phase == PROXY_S_UPSTREAM_CONNECTED ||
                                     cxt->phase == PROXY_S_UPSTREAM_REQUEST_SENDING)) {
            printf("Ignoring client fd %d as phase is %d\n", fd, cxt->phase);
            return 0;
        }

        if (fd == cxt->upstream_fd) {
            printf("Upstream is now writable\n");
            cxt->phase = PROXY_S_UPSTREAM_REQUEST_SENDING;
        } else {
            cxt->phase == PROXY_S_CLIENT_RESPONSE_SENDING;
        }

        while(1) {
            int sent = send(fd, &cxt->buffer->buf[cxt->buffer_cursor], cxt->buffer->size - cxt->buffer_cursor, 0);
            cxt->buffer_cursor += sent;
            if (cxt->buffer_cursor == cxt->buffer->size) {
                printf("Request completely sent to the upstream.\n");
                if (cxt->phase == PROXY_S_UPSTREAM_REQUEST_SENDING) cxt->phase = PROXY_S_UPSTREAM_REQUEST_SENT;

                // cleanup.
                if (fd == cxt->client_fd) {
                    server_del_socket_from_epoll(epfd, scxt);
                    close(cxt->upstream_fd); // TODO: Respond to the client instead.
                    server_free_context(scxt);
                }
                break;
            }
            if (sent < 0) {
                // TODO: Log warning
                break;
            }
        }
    }
}
