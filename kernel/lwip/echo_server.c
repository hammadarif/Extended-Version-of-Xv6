// kernel/lwip/echo_server.c
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "printf.h" // Required for debug prints in XV6

// Called when data is received from the client
static err_t echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  if (p == NULL) {
    printf("[echo] Connection closed by client.\n");
    tcp_close(tpcb);
    return ERR_OK;
  }

  printf("[echo] Received %d bytes from client.\n", p->len);

  // Echo the data back
  err_t werr = tcp_write(tpcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
  if (werr != ERR_OK) {
    printf("[echo] tcp_write failed with error code: %d\n", werr);
  } else {
    printf("[echo] Echoed back to client.\n");
    tcp_output(tpcb);  // Important: flush pending TCP output
  }

  pbuf_free(p);  // Free received packet buffer
  return ERR_OK;
}

// Called when a new client connects
static err_t echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
  printf("[echo] New connection accepted.\n");
  tcp_recv(newpcb, echo_recv);  // Set the receive handler
  return ERR_OK;
}

// Entry point to start the echo server
void echo_server_init(void) {
  struct tcp_pcb *pcb = tcp_new();
  if (pcb == NULL) {
    printf("[echo] Failed to create PCB\n");
    return;
  }

  err_t bind_err = tcp_bind(pcb, IP_ADDR_ANY, 5000);
  if (bind_err != ERR_OK) {
    printf("[echo] Failed to bind: %d\n", bind_err);
    return;
  }

  pcb = tcp_listen(pcb);
  tcp_accept(pcb, echo_accept);
  printf("[echo] Echo server is listening on port 5000...\n");
}