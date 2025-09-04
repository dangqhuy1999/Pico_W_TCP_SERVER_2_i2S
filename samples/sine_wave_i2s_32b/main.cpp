/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pico/multicore.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "project_config.h"
#include "pico/audio.h"
#include "pico/audio_i2s.h"

#define SINE_WAVE_TABLE_LEN 2048
#define SAMPLES_PER_BUFFER 1024  // frames / channel
#define BUFFER_COUNT       12    // số buffer trong pool
#define USE_RING_BUFFER 1   // 0 = không dùng ring buffer, 1 = dùng ring buffer

static const uint32_t PIN_DCDC_PSM_CTRL = 23;

audio_buffer_pool_t *ap;

static bool decode_flg = false;
static constexpr int32_t DAC_ZERO = 1;

#define audio_pio __CONCAT(pio, PICO_AUDIO_I2S_PIO)

static audio_format_t audio_format = {
    .sample_freq = 16000,
    .pcm_format = AUDIO_PCM_FORMAT_S16,
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 4
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = PICO_AUDIO_I2S_DMA_CHANNEL,
    .dma_channel1 = PICO_AUDIO_I2S_DMA_CHANNEL + 1,
    .pio_sm = 0
};

static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];
uint32_t step0 = 0x200000;
uint32_t step1 = 0x200000;
uint32_t pos0 = 0;
uint32_t pos1 = 0;
const uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
uint vol = 20;


#if 0
audio_buffer_pool_t *init_audio() {

    static audio_format_t audio_format = {
        .pcm_format = AUDIO_PCM_FORMAT_S32,
        .sample_freq = 44100,
        .channel_count = 2
    };

    static audio_buffer_format_t producer_format = {
        .format = &audio_format,
        .sample_stride = 8
    };

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, 3,
                                                                      SAMPLES_PER_BUFFER); // todo correct size
    bool __unused ok;
    const audio_format_t *output_format;
#if USE_AUDIO_I2S
    audio_i2s_config_t config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = 0,
        .pio_sm = 0
    };

    output_format = audio_i2s_setup(&audio_format, &audio_format, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *buffer = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *) buffer->buffer->bytes;
        for (uint i = 0; i < buffer->max_sample_count; i++) {
            samples[i*2+0] = 0;
            samples[i*2+1] = 0;
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(producer_pool, buffer);
    }
    audio_i2s_set_enabled(true);
#elif USE_AUDIO_PWM
    output_format = audio_pwm_setup(&audio_format, -1, &default_mono_channel_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    ok = audio_pwm_default_connect(producer_pool, false);
    assert(ok);
    audio_pwm_set_enabled(true);
#elif USE_AUDIO_SPDIF
    output_format = audio_spdif_setup(&audio_format, &audio_spdif_default_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    //ok = audio_spdif_connect(producer_pool);
    ok = audio_spdif_connect(producer_pool);
    assert(ok);
    audio_spdif_set_enabled(true);
#endif
    return producer_pool;
}
#endif


// ====== cấu hình ring buffer ======
#if USE_RING_BUFFER
#define RING_BUFFER_SIZE 8192  // byte, tuỳ RAM
static uint8_t ring_buffer[RING_BUFFER_SIZE];
static volatile uint16_t rb_head = 0, rb_tail = 0;

static inline uint16_t rb_available(void) {
    return (rb_head >= rb_tail) ? (rb_head - rb_tail)
                                : (RING_BUFFER_SIZE - (rb_tail - rb_head));
}

static inline uint16_t rb_space(void) {
    return RING_BUFFER_SIZE - rb_available() - 1;
}

static void rb_write(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        ring_buffer[rb_head] = data[i];
        rb_head = (rb_head + 1) % RING_BUFFER_SIZE;
    }
}

static uint16_t rb_read(uint8_t *dst, uint16_t len) {
    uint16_t cnt = 0;
    while (cnt < len && rb_tail != rb_head) {
        dst[cnt++] = ring_buffer[rb_tail];
        rb_tail = (rb_tail + 1) % RING_BUFFER_SIZE;
    }
    return cnt;
}
#endif

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    bool complete;
    uint8_t buffer_sent[BUF_SIZE];
    uint8_t buffer_recv[BUF_SIZE];
    int sent_len;
    int recv_len;
    int run_count;
} TCP_SERVER_T;

audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq)
{
    audio_format.sample_freq = sample_freq;

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format,
                                                                 BUFFER_COUNT,
                                                                 SAMPLES_PER_BUFFER);
    ap = producer_pool;

    const audio_format_t *output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool __unused ok = audio_i2s_connect(producer_pool);
    assert(ok);

    // initial buffer = silence
    audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
    int16_t *samples = (int16_t *) ab->buffer->bytes;
    for (uint i = 0; i < ab->max_sample_count; i++) {
        samples[i*2+0] = DAC_ZERO;
        samples[i*2+1] = DAC_ZERO;
    }
    ab->sample_count = ab->max_sample_count;
    give_audio_buffer(producer_pool, ab);

    audio_i2s_set_enabled(true);
    decode_flg = true;

    return producer_pool;
}


void i2s_audio_deinit()
{
    decode_flg = false;

    audio_i2s_set_enabled(false);
    audio_i2s_end();

    audio_buffer_t* ab;
    ab = take_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(ap, false);
    }
    ab = get_free_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(ap, false);
    }
    ab = get_full_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(ap, false);
    }
    free(ap);
    ap = nullptr;
}

void decode()
{
    audio_buffer_t *buffer = take_audio_buffer(ap, false);
    if (buffer == NULL) { return; }
    int32_t *samples = (int32_t *) buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        int32_t value0 = (vol * sine_wave_table[pos0 >> 16u]) << 8u;
        int32_t value1 = (vol * sine_wave_table[pos1 >> 16u]) << 8u;
        // use 32bit full scale
        samples[i*2+0] = value0 + (value0 >> 16u);  // L
        samples[i*2+1] = value1 + (value1 >> 16u);  // R
        pos0 += step0;
        pos1 += step1;
        if (pos0 >= pos_max) pos0 -= pos_max;
        if (pos1 >= pos_max) pos1 -= pos_max;
    }
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
    return;
}

extern "C" {
// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at my_pico_audio_i2s/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func()
{
    if (decode_flg) {
        decode();
    }
}
}  

static TCP_SERVER_T* tcp_server_init(void) {
    TCP_SERVER_T *state = (TCP_SERVER_T *)calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    return state;
}

static err_t tcp_server_close(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;
    if (state->client_pcb != NULL) {
        tcp_arg(state->client_pcb, NULL);
        tcp_recv(state->client_pcb, NULL);
        tcp_err(state->client_pcb, NULL);
        err = tcp_close(state->client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->client_pcb);
            err = ERR_ABRT;
        }
        state->client_pcb = NULL;
    }
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
    return err;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        printf("Client closed the connection.\n");
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

#if USE_RING_BUFFER
    // --- Phiên bản có ring buffer ---
    // Ghi toàn bộ dữ liệu pbuf vào ring buffer
    rb_write(&audio_ringbuf, (uint8_t *)p->payload, p->tot_len);

    tcp_recved(tpcb, p->tot_len);  // báo đã nhận hết
    // consumer loop (task riêng) sẽ đọc ring buffer -> copy sang audio_buffer_pool

#else
    // --- Phiên bản không ring buffer (enqueue trực tiếp vào pool) ---
    u16_t remaining = p->tot_len;
    u16_t offset = 0;
    u16_t consumed = 0;

    while (remaining > 0) {
        audio_buffer_t *buffer = take_audio_buffer(ap, false);
        if (buffer == NULL) {
            // Pool cạn → dừng
            break;
        }

        int cap_bytes = buffer->max_sample_count * producer_format.sample_stride;
        int to_copy   = (remaining > cap_bytes) ? cap_bytes : remaining;

        pbuf_copy_partial(p, buffer->buffer->bytes, to_copy, offset);
        buffer->sample_count = to_copy / producer_format.sample_stride;

        give_audio_buffer(ap, buffer);

        offset    += to_copy;
        remaining -= to_copy;
        consumed  += to_copy;
    }

    if (consumed > 0) {
        tcp_recved(tpcb, consumed);
    }

    if (remaining > 0) {
        printf("Backpressure: dropped %u bytes (no audio buffers)\n", remaining);
    }
#endif

    pbuf_free(p);
    return ERR_OK;
}

#if USE_RING_BUFFER
// Task chạy nền: lấy từ ring buffer ra audio pool
void audio_feed_task(void) {
    while (1) {
        audio_buffer_t *buffer = take_audio_buffer(ap, true);
        if (!buffer) continue;

        int cap_bytes = buffer->max_sample_count * producer_format.sample_stride;
        int got = rb_read(buffer->buffer->bytes, cap_bytes);
        buffer->sample_count = got / producer_format.sample_stride;

        give_audio_buffer(ap, buffer);
    }
}
#endif


static void tcp_server_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("Failure in accept\n");
        return ERR_VAL;
    }
    DEBUG_printf("Client connected\n");

    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

void run_tcp_server_test(void) {
    TCP_SERVER_T *state = tcp_server_init();
    if (!state) {
        return;
    }
    if (!tcp_server_open(state)) {
        return;
    }
    while(true) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for Wi-Fi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }
    free(state);
}

#if USE_RING_BUFFER
void core1_entry() {
    while (true) {
        audio_feed_task();   // vòng lặp blocking đọc ring buffer → audio
    }
}
#endif

int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    }

    printf("Wi-Fi Connected.\n");
    ap = i2s_audio_init(16000);

#if USE_RING_BUFFER
    multicore_launch_core1(core1_entry);   // chạy audio_feed_task ở core1
#endif

    run_tcp_server_test();  // core0 lo TCP server
    cyw43_arch_deinit();
    return 0;
}