#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>

// cấu hình dung lượng ring buffer (tuỳ RAM)
#define RING_BUFFER_SIZE 8192  

typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} ringbuf_t;

#ifdef __cplusplus
extern "C" {
#endif

// API
void rb_init(ringbuf_t *rb);
uint16_t rb_available(ringbuf_t *rb);
uint16_t rb_space(ringbuf_t *rb);
void rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len);
uint16_t rb_read(ringbuf_t *rb, uint8_t *dst, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // RINGBUF_H
