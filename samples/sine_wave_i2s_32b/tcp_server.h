#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "pico/sync.h"
#include "lwip/tcp.h"
#include "ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khai báo cấu trúc
typedef struct TCP_SERVER_T_ TCP_SERVER_T;

// Khai báo hàm để khởi động TCP server.
// Nó cần nhận con trỏ tới ring buffer và spin lock để truyền cho hàm callback.
void run_tcp_server_test(ringbuf_t *ringbuf, spin_lock_t *lock);

// Khai báo hàm callback để nhận dữ liệu.
// Hàm này cũng cần nhận con trỏ tới ring buffer và spin lock.
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H