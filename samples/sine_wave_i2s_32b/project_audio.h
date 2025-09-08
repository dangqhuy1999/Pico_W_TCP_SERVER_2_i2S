#ifndef PROJECT_AUDIO_H
#define PROJECT_AUDIO_H

#include <stdint.h>
#include "pico/audio.h"
#include "pico/audio_i2s.h" // Vẫn cần include thư viện gốc của Pico
#include "ringbuf.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "project_config.h"
#include "tcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif


extern audio_buffer_pool_t *ap;

// Khởi tạo hệ thống audio I2S
audio_buffer_pool_t *project_audio_init(uint32_t sample_freq);


void project_audio_check_and_resume_tcp(struct tcp_pcb *tpcb, ringbuf_t *rb, spin_lock_t *lock);

// Hàm được chạy trên Core 1 để lấy dữ liệu từ ring buffer và gửi tới I2S
void project_audio_feed_task(ringbuf_t *rb, spin_lock_t *lock, TCP_SERVER_T *tcp_state);


#ifdef __cplusplus
}
#endif

#endif // PROJECT_AUDIO_H