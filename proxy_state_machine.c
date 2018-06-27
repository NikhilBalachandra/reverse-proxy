#include <stdio.h>
#include <assert.h>

#include "http_parser/http_parser.h"
#include "stream_buffer.h"
#include "proxy_state_machine.h"

int on_message_complete(http_parser *parser)
{
    printf("HTTP message fully received\n");
    struct proxy_context *cxt = parser->data;
    if (cxt->phase == PROXY_S_CLIENT_REQUEST_RECEIVING) {
        cxt->phase = PROXY_S_CLIENT_REQUEST_RECEIVED;
    } else {
        cxt->phase = PROXY_S_UPSTREAM_RESPONSE_RECEIVED;
    }
    return 0;
}

int proxy_context_init(struct proxy_context *cxt)
{
    cxt->phase = PROXY_S_INIT_OR_FREE;
    cxt->client_fd = -1;
    cxt->upstream_fd = -1;
    cxt->buffer_cursor = 0;
    cxt->buffer = malloc(sizeof(stream_buffer_t));
    if (stream_buffer_t_init(cxt->buffer, INITIAL_BUFFER_CAPACITY) < 0) {
        return -1;
    }
    cxt->parser = malloc(sizeof(http_parser));
    if (cxt->parser == NULL) {
        stream_buffer_t_free(cxt->buffer);
        return -1;
    }
    http_parser_init(cxt->parser, HTTP_REQUEST);
    return 0;
}

void proxy_context_free(struct proxy_context *cxt)
{
    cxt->phase = PROXY_S_INIT_OR_FREE;
    cxt->client_fd = -1;
    cxt->upstream_fd = -1;
    cxt->buffer_cursor = 0;
    stream_buffer_t_free(cxt->buffer);
    free(cxt->buffer);
    free(cxt->parser);
}

inline void proxy_context_set_client(struct proxy_context *cxt, int fd)
{
    assert(cxt->phase == PROXY_S_INIT_OR_FREE);
    cxt->phase = PROXY_S_CLIENT_CONNECTED;
    cxt->client_fd = fd;
}

inline void proxy_context_set_upstream(struct proxy_context *cxt, int fd)
{
    assert(cxt->phase == PROXY_S_CLIENT_REQUEST_RECEIVED);
    cxt->phase = PROXY_S_UPSTREAM_CONNECTION_PROGRESS;
    cxt->upstream_fd = fd;
}

inline void proxy_context_switch_to_response(struct proxy_context *cxt)
{
    http_parser_init(cxt->parser, HTTP_RESPONSE);
}

inline void proxy_context_transit_state(struct proxy_context *cxt, int fd)
{
    printf("cxt->upstream %d, cxt->client %d, fd %d, phase %d\n", cxt->upstream_fd, cxt->client_fd, fd, cxt->phase);
    assert(fd == cxt->client_fd || fd == cxt->upstream_fd);

    if (fd == cxt->client_fd && cxt->phase == PROXY_S_CLIENT_CONNECTED)
        cxt->phase = PROXY_S_CLIENT_REQUEST_RECEIVING;
    else if (fd == cxt->upstream_fd && cxt->phase == PROXY_S_UPSTREAM_REQUEST_SENT)
        cxt->phase = PROXY_S_UPSTREAM_RESPONSE_RECEIVING;
    // else
    //     assert(0);
}
