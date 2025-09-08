#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/audio.h"
#include "pico/audio_i2s.h"

#include "lwipopts.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

// Các file tự định nghĩa
#include "project_config.h"
#include "ringbuf.h"
#include "tcp_server.h" // file chứa hàm tcp_server_recv và run_tcp_server_test
#include "project_audio.h"

// Biến toàn cục được chia sẻ giữa các file
static ringbuf_t audio_ringbuf;
static spin_lock_t *audio_lock; // Khóa để bảo vệ ring buffer
static TCP_SERVER_T* tcp_state;


void core1_entry() {
    while (true) {
        project_audio_feed_task(&audio_ringbuf, audio_lock, tcp_state);
    }
}
int main() {
    stdio_init_all();

    // 1️⃣ Khởi tạo Wi-Fi và kết nối
    if (cyw43_arch_init()) {
        printf("failed to initialise cyw43_arch\n");
        return 1;
    }
    
    cyw43_arch_enable_sta_mode();

    const char *ssid = WIFI_SSID;
    const char *password = WIFI_PASSWORD;

    printf("Connecting to Wi-Fi with SSID: %s\n", ssid);
    printf("TCP_PORT: %d\n", TCP_PORT);

    if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect to Wi-Fi.\n");
        return 1;
    }
    printf("Wi-Fi Connected.\n");

    // 2️⃣ Khởi tạo ring buffer và spin lock
    rb_init(&audio_ringbuf);
    audio_lock = spin_lock_init(spin_lock_claim_unused(true));
    if (!audio_lock) {
        printf("[MAIN] Failed to initialize spin lock.\n");
        return 1;
    }

    // 3️⃣ Khởi tạo hệ thống audio I2S
    audio_buffer_pool_t *ap = project_audio_init(16000); 
    if (!ap) {
        printf("[MAIN] Failed to init I2S audio\n");
        return 1;
    }

    
    
    // 5️⃣ Khởi động TCP server trên Core 0
    tcp_state = run_tcp_server_test(&audio_ringbuf, audio_lock);
    
    // ✅ THÊM LOGIC KIỂM TRA TẠI ĐÂY
    if (!tcp_state) {
        printf("[MAIN] Failed to initialize TCP server. Exiting.\n");
        return 1;
    }

    // 4️⃣ Khởi chạy Core 1 để xử lý âm thanh
    multicore_launch_core1(core1_entry);
    
    // Vòng lặp chính để duy trì kết nối và xử lý sự kiện
    while (true) {
        cyw43_arch_poll();
        sleep_ms(10);
    }

    // Dọn dẹp
    cyw43_arch_deinit();
    return 0;
}