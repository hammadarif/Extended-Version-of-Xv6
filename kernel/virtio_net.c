#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "virtio.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

extern err_t ethernetif_input(struct netif *netif, char *buffer, int len);

extern int register_net_device(struct virtio_net *new_net , int dev_id);

// Size of the actual packet data (not counting virtio_net_hdr).
//#define PACKET_SIZE 1500
#define ALIGNMENT 16
// Number of Rx/Tx slots
//#define NUM 8

#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  printf("[INFO]  " fmt, ##__VA_ARGS__)

struct virtio_net net;
struct netif lwip_netif;


static void fill_rx(int i);

/* ----------------------------------------------------------------------
 *  Low-level "MMIO" helpers
 * ---------------------------------------------------------------------- */
uint32 mmio_read(uint64 addr) {
    return *(volatile uint32 *)(addr);
}

void mmio_write(uint64 addr, uint32 val) {
    *(volatile uint32 *)(addr) = val;
}

/* ----------------------------------------------------------------------
 *  Descriptor allocation
 * ---------------------------------------------------------------------- */

static int alloc_desc(struct virtqueue *q) {
    for (int i = 0; i < 2 * NUM; i++) {
        if (q->free[i]) {
            q->free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(struct virtqueue *q, int idx) {
    if (q->free[idx]) {
        LOG_ERROR("free_desc: already free\n");
        panic("free_desc: already free");
    }
    q->free[idx] = 1;
}
/*
static void reset_device() {
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, 0);
}
*/
/* ----------------------------------------------------------------------
 *  virtio_net_init: Initialize VirtIO Network Device
 * ---------------------------------------------------------------------- */
void virtio_net_init() {
    uint32 status = 0;

    initlock(&net.rx_lock, "virtio_net_rx");
    initlock(&net.tx_lock, "virtio_net_tx");

    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION) != 2 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID) != 1) {
        panic("not a virtio net device");
    }

    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, 0);
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_CONFIG_S_DRIVER;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    uint32 features = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_NET_F_CSUM);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_FEATURES, features);

    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    if (!(mmio_read(VIRTIO1 + VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK)) {
        panic("virtio net: features ok fail");
    }

    init_virtqueue(&net.rx, 0);
    init_virtqueue(&net.tx, 1);

    for (int i = 0; i < NUM; i++) {
        fill_rx(i);
    }

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
}

struct virtio_net *init_virtio_net(int dev_id) {
    if (dev_id < 0 || dev_id >= MAX_NET_DEVICES) {
        LOG_ERROR("Invalid device ID: %d\n", dev_id);
        return NULL;
    }

    virtio_net_init();
    return &net;
}

/* ----------------------------------------------------------------------
 *  Initialize a virtqueue
 * ---------------------------------------------------------------------- */
void init_virtqueue(struct virtqueue *q, int qidx) {
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_SEL, qidx);

    uint32 max = mmio_read(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max < NUM) {
        LOG_ERROR("virtqueue too small\n");
        panic("virtqueue too small");
    }

    q->desc = kalloc();
    q->avail = kalloc();
    q->used = kalloc();

    if (!q->desc || !q->avail || !q->used) {
        LOG_ERROR("virtqueue kalloc failed\n");
        panic("virtqueue kalloc failed");
    }

    memset(q->desc, 0, PGSIZE);
    memset(q->avail, 0, PGSIZE);
    memset(q->used,  0, PGSIZE);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM, NUM);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_LOW,  (uint64)q->desc);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint64)q->desc >> 32);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_LOW,  (uint64)q->avail);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_HIGH, (uint64)q->avail >> 32);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_LOW,  (uint64)q->used);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_HIGH, (uint64)q->used >> 32);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_READY, 1);

    for (int i = 0; i < 2 * NUM; i++) {
        q->free[i] = 1;
        q->buf[i]  = kalloc();
        if (!q->buf[i]) {
            LOG_ERROR("virtqueue buf kalloc failed\n");
            panic("virtqueue buf kalloc failed");
        }
        memset(q->buf[i], 0, PGSIZE);
    }

    q->used_idx = 0;
}

/* ----------------------------------------------------------------------
 *  fill_rx: prepare descriptors for receiving data
 * ---------------------------------------------------------------------- */
static void fill_rx(int i) {
    void *buf = net.rx.buf[i];
    if ((uint64)buf % ALIGNMENT != 0) {
    LOG_ERROR("RX buffer misaligned: %p\n", buf);
    panic("RX buffer alignment");
    }
    net.rx.desc[2*i].addr  = (uint64) buf;
    net.rx.desc[2*i].len   = sizeof(struct virtio_net_hdr);
    net.rx.desc[2*i].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
    net.rx.desc[2*i].next  = 2*i + 1;

    net.rx.desc[2*i+1].addr  = (uint64)((char*)buf + sizeof(struct virtio_net_hdr));
    net.rx.desc[2*i+1].len   = PACKET_SIZE - sizeof(struct virtio_net_hdr);
    net.rx.desc[2*i+1].flags = VRING_DESC_F_WRITE;
    net.rx.desc[2*i+1].next  = 0;

    net.rx.avail->ring[net.rx.avail->idx % NUM] = 2*i;
    __sync_synchronize();
    net.rx.avail->idx++;

    LOG_DEBUG("RX descriptor refilled: idx=%d\n", i);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

/* ----------------------------------------------------------------------
 *  RX: retrieve one packet
 * ---------------------------------------------------------------------- */
void virtio_receive_packet(char *buffer, int buflen, int *received_len) {
    acquire(&net.rx_lock);
    printf("virtio_receive_packet: received_len=%d, buffer=%p\n", *received_len, buffer);
    printf("The value of net.rx.used->idx : %d and the value of net.rx.used_idx : %d \n",net.rx.used->idx,net.rx.used_idx);
    if (net.rx.used->idx == net.rx.used_idx) {
        LOG_DEBUG("No packets available in RX queue.\n");
        *received_len = 0;
        release(&net.rx_lock);
        return;
    }

    int used_i = net.rx.used_idx % NUM;
    int desc_idx = net.rx.used->ring[used_i].id;
    uint len = net.rx.used->ring[used_i].len;

    if (len > sizeof(struct virtio_net_hdr) && len <= buflen) {
        char *buf = net.rx.buf[desc_idx / 2];
        char *pkt_data = buf + sizeof(struct virtio_net_hdr);
        memmove(buffer, pkt_data, len - sizeof(struct virtio_net_hdr));
        *received_len = len - sizeof(struct virtio_net_hdr);
    } else {
        LOG_ERROR("Invalid packet length: %u\n", len);
        *received_len = 0;
    }

    fill_rx(desc_idx / 2);
    if(net.rx.used_idx == 10)
    {
        panic("value of net.rx.used_idx is reached 10");
    }

    LOG_DEBUG("RX packet received: len=%d, desc_idx=%d\n", buflen, desc_idx);
    release(&net.rx_lock);
}

/* ----------------------------------------------------------------------
 *  TX: send a packet
 * ---------------------------------------------------------------------- */
int virtio_send_packet(void *data, uint len) {
    if (!data || len == 0 || len > PACKET_SIZE) {
        LOG_ERROR("Invalid TX packet: data=%p, len=%d\n", data, len);
        return -1;
    }

    printf("Sending packet: data=%p, len=%d\n", data, len);
    acquire(&net.tx_lock);

    if (net.tx.avail->idx - net.tx.used_idx == NUM) {
        LOG_ERROR("TX queue is full. Dropping packet.\n");
        release(&net.tx_lock);
        return -1;
    }
    
    int desc_idx = alloc_desc(&net.tx);
    if (desc_idx < 0) {
        LOG_ERROR("Failed to allocate TX descriptor\n");
        release(&net.tx_lock);
        return -1;
    }

    void *buf = net.tx.buf[desc_idx];
    if ((uint64)buf % ALIGNMENT != 0) {
        LOG_ERROR("TX buffer misaligned: %p\n", buf);
        panic("TX buffer alignment");
    }
    memmove(buf, data, len);

    net.tx.desc[desc_idx].addr  = (uint64)buf;
    net.tx.desc[desc_idx].len   = len;
    net.tx.desc[desc_idx].flags = 0;
    net.tx.desc[desc_idx].next  = 0;

    net.tx.avail->ring[net.tx.avail->idx % NUM] = desc_idx;
    __sync_synchronize();

    net.tx.avail->idx++;

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 1);
    LOG_DEBUG("TX packet sent: len=%d, desc_idx=%d\n", len, desc_idx);
    release(&net.tx_lock);
    
    return 0;
}

/* ----------------------------------------------------------------------
 *  virtio_net_intr: handle interrupts
 * ---------------------------------------------------------------------- */
void virtio_net_intr() {
    uint32 irq_status = mmio_read(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS);
    int rx_used = 0;
    if (!irq_status) return;
    LOG_DEBUG("VirtIO Interrupt: status=0x%x\n", irq_status);
    acquire(&net.rx_lock);
    while (net.rx.used->idx != net.rx.used_idx) {
        int used_i = net.rx.used_idx % NUM;
        int desc_idx = net.rx.used->ring[used_i].id;
        LOG_DEBUG("RX descriptor used: desc_idx=%d, used_idx=%d\n", desc_idx, net.rx.used_idx);
        char buffer[PACKET_SIZE];
        int received_len = 0;
        release(&net.rx_lock);
        virtio_receive_packet(buffer, sizeof(buffer), &received_len);
        acquire(&net.rx_lock);
        printf("The value of recieve len %d \n",received_len);
        if (received_len > 0) {
            err_t result = ethernetif_input(&lwip_netif, buffer, received_len);
            if (result != ERR_OK) {
                LOG_ERROR("ethernetif_input failed with error: %d\n", result);
            }else
            {
                rx_used = 1;
            }
        }
        net.rx.used_idx++;
    }
    if(rx_used)
    {
        release(&net.rx_lock);
        mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_ACK, irq_status);
        LOG_DEBUG("VirtIO interrupt acknowledged\n");
        return;
    }
    release(&net.rx_lock);

    acquire(&net.tx_lock);
    while (net.tx.used->idx != net.tx.used_idx) {
        int used_i = net.tx.used_idx % NUM;
        int desc_idx = net.tx.used->ring[used_i].id;
        LOG_DEBUG("TX descriptor used: desc_idx=%d, used_idx=%d\n", desc_idx, net.tx.used_idx);
        free_desc(&net.tx, desc_idx);
        net.tx.used_idx++;
    }
    release(&net.tx_lock);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_ACK, irq_status);
    LOG_DEBUG("VirtIO interrupt acknowledged\n");
}

/* ----------------------------------------------------------------------
 *  lwIP integration stubs 
 * ---------------------------------------------------------------------- */
void lwip_init_network() {
    lwip_init();
    ip4_addr_t ipaddr, netmask, gw;
    // Make the guest IP 10.0.2.15 (arbitrary .15)
    IP4_ADDR(&ipaddr, 10, 0, 2, 15);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    // QEMU's user-mode NAT gateway
    IP4_ADDR(&gw, 10, 0, 2, 2);

    netif_add(&lwip_netif, &ipaddr, &netmask, &gw,
              NULL, ethernetif_init, tcpip_input);
    netif_set_default(&lwip_netif);
    netif_set_up(&lwip_netif);
}