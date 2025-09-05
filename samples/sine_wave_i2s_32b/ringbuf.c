#include "ringbuf.h"

void rb_init(ringbuf_t *rb) {
    rb->head = rb->tail = 0;
}

uint16_t rb_available(ringbuf_t *rb) {
    return (rb->head >= rb->tail)
        ? (rb->head - rb->tail)
        : (RING_BUFFER_SIZE - (rb->tail - rb->head));
}

uint16_t rb_space(ringbuf_t *rb) {
    return RING_BUFFER_SIZE - rb_available(rb) - 1;
}

void rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    }
}

uint16_t rb_read(ringbuf_t *rb, uint8_t *dst, uint16_t len) {
    uint16_t cnt = 0;
    while (cnt < len && rb->tail != rb->head) {
        dst[cnt++] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    }
    return cnt;
}
