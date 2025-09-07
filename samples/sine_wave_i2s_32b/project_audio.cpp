#include "project_audio.h"
#include <stdio.h>
#include <string.h>
#include "pico/audio_i2s.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"

// Các biến toàn cục
audio_buffer_pool_t *ap;
// Thay đổi DAC_ZERO thành 32-bit và giá trị 1
static constexpr int32_t DAC_ZERO = 1;

// Cấu hình audio
static audio_format_t audio_format = {
    .sample_freq = 16000,
    .pcm_format = AUDIO_PCM_FORMAT_S32, // Đã chính xác
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8 // 2 kênh, mỗi kênh 4 byte (S32)
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = 4,
    .dma_channel1 = 5,
    .pio_sm = 0
};

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

audio_buffer_pool_t *project_audio_init(uint32_t sample_freq) {
    audio_format.sample_freq = sample_freq;
    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    ap = producer_pool;

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
        // Thay đổi con trỏ mẫu thành 32-bit
        int32_t *samples = (int32_t *) ab->buffer->bytes;
        // Vòng lặp khởi tạo buffer với giá trị 32-bit
        for (uint i = 0; i < ab->max_sample_count * 2; i++) {
            samples[i] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(producer_pool, ab);
    }
    audio_i2s_set_enabled(true);
    return producer_pool;
}

void project_audio_feed_task(ringbuf_t *rb, spin_lock_t *lock) {
    if (!ap) {
        return;
    }
    
    // Lấy một buffer rỗng từ pool và chờ nếu cần
    audio_buffer_t *buffer = take_audio_buffer(ap, true);
    if (buffer == nullptr) {
        return;
    }
    // Thay đổi con trỏ mẫu thành 32-bit
    int32_t *samples = (int32_t *)buffer->buffer->bytes;
    size_t samples_per_buffer = buffer->max_sample_count;
    // Thay đổi tính toán bytes_to_read cho 32-bit
    size_t bytes_to_read = samples_per_buffer * sizeof(int32_t) * 2; // 2 kênh, mỗi mẫu 4 byte (S32)

    // Dùng spin lock để đảm bảo an toàn khi truy cập ring buffer
    uint32_t flags = spin_lock_blocking(lock);

    size_t bytes_available = rb_available(rb);

    // Kiểm tra xem có đủ dữ liệu để điền đầy buffer không
    if (bytes_available >= bytes_to_read) {
        rb_read(rb, (uint8_t *)samples, bytes_to_read);
    } else {
        // Nếu không đủ, đọc hết dữ liệu có sẵn và điền 0 vào phần còn lại
        size_t bytes_read = rb_read(rb, (uint8_t *)samples, bytes_available);
        size_t samples_read = bytes_read / sizeof(int32_t);
        for (size_t i = samples_read; i < samples_per_buffer * 2; ++i) {
            samples[i] = DAC_ZERO;
        }
    }

    spin_unlock(lock, flags);

    buffer->sample_count = samples_per_buffer;
    give_audio_buffer(ap, buffer);
}