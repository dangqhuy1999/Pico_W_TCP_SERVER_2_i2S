#include "tcp_server.h"
#include "pico/cyw43_arch.h"
#include "project_config.h"
#include <string.h>
#include <stdlib.h>


// Thêm hàm này vào tcp_server.c
void tcp_server_resume(struct tcp_pcb *pcb) {
    if (pcb) {
        tcp_recv(pcb, tcp_server_recv);
    }
}

static TCP_SERVER_T* tcp_server_init(ringbuf_t *ringbuf, spin_lock_t *lock) {
    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        printf("failed to allocate state\n");
        return NULL;
    }
    state->ringbuf = ringbuf;
    state->lock = lock;
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
        state->client_pcb = NULL;
    }
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
    free(state);
    return err;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    if (!p) {
        printf("Client closed the connection. Transmission complete.\n");
        // Báo cho lwIP rằng server đã sẵn sàng nhận thêm dữ liệu
        // Khi p == NULL, bạn không cần gọi tcp_recved.
        // Chỉ cần return ERR_OK.
        // tcp_recved(tpcb, p->tot_len);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        printf("tcp_server_recv: error %d\n", err);
        pbuf_free(p);
        return err;
    }

    // Lấy con trỏ đến ring buffer và lock từ cấu trúc state
    ringbuf_t *rb = state->ringbuf;
    spin_lock_t *lock = state->lock;

    // Khóa spin lock để đảm bảo an toàn luồng
    uint32_t flags = spin_lock_blocking(lock);

    // Kiểm tra xem ring buffer có đủ chỗ không
    uint16_t space_available = rb_space(rb);
    printf("[CORE0] Received %d bytes. Ring buffer space: %d\n", p->tot_len, space_available);

    if (space_available < p->tot_len) {
        printf("Ring buffer full! Waiting for space...\n");
        spin_unlock(lock, flags);
        
        // Trả về ERR_WOULDBLOCK để thông báo cho lwIP rằng
        // gói tin không thể xử lý ngay lập tức.
        // Gói tin sẽ không bị giải phóng và lwIP sẽ cố gắng gửi lại
        // hoặc đợi đến lần gọi tcp_recv tiếp theo.
        return ERR_WOULDBLOCK;
    }

    // Nếu đủ, ghi toàn bộ dữ liệu từ pbuf vào ring buffer
    rb_write(rb, (uint8_t *)p->payload, p->tot_len);

    // Mở khóa
    spin_unlock(lock, flags);

    // Báo cho lwIP biết dữ liệu đã được xử lý thành công
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    
    // In ra mã lỗi nếu không phải là lỗi kết nối bị hủy bỏ (ERR_ABRT)
    if (err != ERR_ABRT) {
        printf("tcp_server_err: %d - %s\n", err, lwip_strerr(err));
    }
    
    // Giải phóng tài nguyên và đặt lại trạng thái khi có lỗi
    // Quan trọng: Phải gọi hàm này để dọn dẹp kết nối TCP
    tcp_server_close(state);
    
    // Đặt lại con trỏ client_pcb thành NULL để báo hiệu không có kết nối
    state->client_pcb = NULL;

    printf("TCP connection closed due to error or remote host disconnect.\n");
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        printf("Failure in accept\n");
        tcp_server_close(state);
        return ERR_VAL;
    }
    printf("Client connected\n");

    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        printf("failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    // Dòng này sẽ hiển thị IP và cổng sau khi server đã sẵn sàng lắng nghe
    printf("Listening on %s port %d\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);
    return true;
}

TCP_SERVER_T* run_tcp_server_test(ringbuf_t *ringbuf, spin_lock_t *lock) {
    TCP_SERVER_T *state = tcp_server_init(ringbuf, lock);
    
    if (!state) {
        printf("[TCP] Failed to initialize TCP server state.\n");
        return NULL; // Trả về NULL khi không thể khởi tạo
    }
    
    if (!tcp_server_open(state)) {
        printf("[TCP] Failed to open TCP server. Closing state.\n");
        // Hàm tcp_server_close sẽ dọn dẹp state
        tcp_server_close(state); 
        return NULL; // Trả về NULL khi không thể mở server
    }
    
    printf("[TCP] TCP server is listening on port %d...\n", TCP_PORT);
    return state; // Trả về con trỏ trạng thái khi thành công
}