#include <stdlib.h>
#include <string.h>
#include "stream_buffer.h"

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

int stream_buffer_t_init(stream_buffer_t *buf, int capacity)
{
    buf->buf = malloc(sizeof(char) * capacity);
    if (buf->buf == NULL) {
        return -1;
    }
    buf->capacity = capacity;
    buf->size = 0;
    return 0;
}

int stream_buffer_t_resize(stream_buffer_t *buf, uint hint_new_size)
{
    uint new_capacity = 1.5 * max(buf->capacity, hint_new_size);
    char *temp_buf = realloc(buf->buf, sizeof(char) * new_capacity);
    if (temp_buf == NULL) {
        return -1;
    }
    buf->buf = temp_buf;
    buf->capacity = new_capacity;
    return 0;
}

int stream_buffer_t_append(stream_buffer_t *buf, char *source, uint source_len)
{
    if (buf->size + source_len > buf->capacity) {
        if (stream_buffer_t_resize(buf, buf->size + source_len) != 0) {
            return -1;
        }
    }
    strncpy(&buf->buf[buf->size], source, source_len);
    buf->size += source_len;
    return 0;
}

void stream_buffer_t_free(stream_buffer_t *buf)
{
    free(buf->buf);
    buf->capacity = 0;
    buf->size = 0;
}
