#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include "lwip/pbuf.h"
#include "pico/sync.h"
#include "lwip/tcp.h"
#include "ringbuf.h"
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

// Định nghĩa cấu trúc, chỉ cần trong file .c
typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    ringbuf_t *ringbuf;
    spin_lock_t *lock;
} TCP_SERVER_T;

// Khai báo hàm để khởi động TCP server.
// Nó cần nhận con trỏ tới ring buffer và spin lock để truyền cho hàm callback.
TCP_SERVER_T* run_tcp_server_test(ringbuf_t *ringbuf, spin_lock_t *lock);
// Khai báo hàm callback để nhận dữ liệu.
// Hàm này cũng cần nhận con trỏ tới ring buffer và spin lock.
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Thêm hàm kích hoạt lại TCP
void tcp_server_resume(struct tcp_pcb *pcb);



#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H