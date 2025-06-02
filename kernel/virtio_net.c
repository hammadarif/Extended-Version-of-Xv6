// virtio_net.c - Merged version combining lwIP integration and robustness

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "virtio.h"
#include "memlayout.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"


#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

#define PACKET_SIZE 1500
#define ALIGNMENT 16
#define DEBUG_NET 1

#if DEBUG_NET
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  printf("[INFO]  " fmt, ##__VA_ARGS__)

extern err_t ethernetif_input(struct netif *netif, char *buffer, int len);

struct virtio_net net;
struct netif lwip_netif;

static void fill_rx(int i);

uint32 mmio_read(uint64 addr) {
    return *(volatile uint32 *)(addr);
}

void mmio_write(uint64 addr, uint32 val) {
    *(volatile uint32 *)(addr) = val;
}

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
        panic("free_desc: already free");
    }
    q->free[idx] = 1;
}

void init_virtqueue(struct virtqueue *q, int qidx) {
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_SEL, qidx);
    uint32 max = mmio_read(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max < NUM) panic("virtqueue too small");

    q->desc = kalloc();
    q->avail = kalloc();
    q->used = kalloc();

    if (!q->desc || !q->avail || !q->used) panic("virtqueue kalloc failed");

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
        if (!q->buf[i]) panic("virtqueue buf kalloc failed");
        memset(q->buf[i], 0, PGSIZE);
    }
    q->used_idx = 0;
}

static void fill_rx(int i) {
    void *buf = net.rx.buf[i];
    if ((uint64)buf % ALIGNMENT != 0) panic("RX buffer alignment");

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
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

void virtio_receive_packet(char *buffer, int buflen, int *received_len) {
    acquire(&net.rx_lock);
    if (net.rx.used->idx == net.rx.used_idx) {
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
        *received_len = 0;
    }

    fill_rx(desc_idx / 2);
    net.rx.used_idx++;
    release(&net.rx_lock);
}

int virtio_send_packet(void *data, uint len) {
    printf("Virtio_send_packet called with len=%d\n", len);
    if (!data || len == 0 || len > PACKET_SIZE) {
        printf("Invalid TX packet: len=%d\n", len);
        return -1;
    }

    acquire(&net.tx_lock);

    if (net.tx.avail->idx - net.tx.used_idx == NUM) {
        printf("TX ring full\n");
        release(&net.tx_lock);
        return -1;
    }

    int desc_idx = alloc_desc(&net.tx);
    if (desc_idx < 0) {
        printf("No free TX descriptor\n");
        release(&net.tx_lock);
        return -1;
    }

    void *buf = net.tx.buf[desc_idx];
    memset(buf, 0, PGSIZE); // 🛡️ ensures no garbage leaks

    //struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)buf;
    // no flags needed: let all be 0
    // hdr->flags = 0;

    void *payload = (void *)((char *)buf + sizeof(struct virtio_net_hdr));
    memmove(payload, data, len); // ✅ Ethernet frame after hdr

    net.tx.desc[desc_idx].addr  = (uint64)buf;
    net.tx.desc[desc_idx].len   = sizeof(struct virtio_net_hdr) + len;
    net.tx.desc[desc_idx].flags = 0;
    net.tx.desc[desc_idx].next  = 0;

    // optional: dump first few bytes for verification
    printf("[TX] Final descriptor length = %d\n", net.tx.desc[desc_idx].len);
    printf("[TX] Packet dump (first 32 bytes after hdr):\n");
    for (int i = 0; i < len && i < 32; i++) {
        printf("%x ", ((uint8_t*)payload)[i]);
    }
    printf("\n");

    net.tx.avail->ring[net.tx.avail->idx % NUM] = desc_idx;
    __sync_synchronize();
    net.tx.avail->idx++;
    printf("TX descriptor %d added, idx now %d\n", desc_idx, net.tx.avail->idx);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 1);

    release(&net.tx_lock);
    return 0;
}
void virtio_net_intr() {
    printf("Intr_virtio_net: IRQ received\n");
    uint32 irq_status = mmio_read(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS);
    if (!irq_status) return;

    acquire(&net.rx_lock);
    while (net.rx.used->idx != net.rx.used_idx) {
        //int used_i = net.rx.used_idx % NUM;
        //int desc_idx = net.rx.used->ring[used_i].id;
        char buffer[PACKET_SIZE];
        int received_len = 0;
        release(&net.rx_lock);
        virtio_receive_packet(buffer, sizeof(buffer), &received_len);
        acquire(&net.rx_lock);
        if (received_len > 0)
            ethernetif_input(&lwip_netif, buffer, received_len);
    }
    release(&net.rx_lock);

    acquire(&net.tx_lock);
    while (net.tx.used->idx != net.tx.used_idx) {
        int used_i = net.tx.used_idx % NUM;
        int desc_idx = net.tx.used->ring[used_i].id;
        free_desc(&net.tx, desc_idx);
        net.tx.used_idx++;
    }
    release(&net.tx_lock);
    
    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_ACK, irq_status);
    printf("Intr_virtio_net: IRQ processed\n");
}
struct virtio_net *init_virtio_net(int dev_id) {
    if (dev_id < 0 || dev_id >= MAX_NET_DEVICES) {
        LOG_ERROR("Invalid device ID: %d\n", dev_id);
        return NULL;
    }

    virtio_net_init();
    return &net;
}
void virtio_net_init() {
    uint32 status = 0;

    // Initialize spinlocks
    initlock(&net.rx_lock, "virtio_net_rx");
    initlock(&net.tx_lock, "virtio_net_tx");

    // Check for VirtIO magic values and device ID
    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION) != 2 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID) != 1) {
        panic("virtio_net_init: not a virtio network device");
    }

    // Step 1: Reset device
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, 0);

    // Step 2: Acknowledge the device
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    // Step 3: Announce the driver
    status |= VIRTIO_CONFIG_S_DRIVER;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    // Step 4: Read device-supported features
    uint32 features = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_FEATURES);

    // Step 5: Disable unwanted features
    features &= ~(1 << VIRTIO_NET_F_CSUM);
    features &= ~(1 << VIRTIO_NET_F_GUEST_CSUM);
    features &= ~(1 << VIRTIO_NET_F_GUEST_TSO4);
    features &= ~(1 << VIRTIO_NET_F_GUEST_TSO6);
    features &= ~(1 << VIRTIO_NET_F_HOST_TSO4);
    features &= ~(1 << VIRTIO_NET_F_HOST_TSO6);
    features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    //features &= ~(1 << VIRTIO_NET_F_CTRL_VQ);
    features &= ~(1ULL << VIRTIO_NET_F_CTRL_VQ);
    features &= ~(1 << VIRTIO_NET_F_CTRL_RX);
    features &= ~(1 << VIRTIO_NET_F_CTRL_MAC_ADDR);

    // Step 6: Require MAC support and version 1
    if (!(features &  VIRTIO_NET_F_MAC)) {
        panic("virtio_net_init: device does not support MAC");
    }

    //features |= (1 << VIRTIO_NET_F_MAC);
    features |= VIRTIO_NET_F_MAC;
    //features |= (1 << VIRTIO_F_VERSION_1);
    features |= (1ULL << VIRTIO_F_VERSION_1);
    // Step 7: Write negotiated features
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_FEATURES, features);

    // Step 8: Finalize feature negotiation
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    // Step 9: Confirm FEATURES_OK bit is accepted
    if (!(mmio_read(VIRTIO1 + VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK)) {
        panic("virtio_net_init: FEATURES_OK not set");
    }

    // Step 10: Initialize RX and TX queues
    init_virtqueue(&net.rx, 0);
    init_virtqueue(&net.tx, 1);

    // Step 11: Fill RX buffers
    for (int i = 0; i < NUM; i++) {
        fill_rx(i);
    }

    /* virtio-net initialisation */
    //uint64_t host = mmio_read64(VIRTIO_MMIO_DEVICE_FEATURES);
    //uint64_t guest = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC;   /* nothing else yet */
    //mmio_write64(VIRTIO_MMIO_GUEST_FEATURES, guest & host);
    // Optional: read MAC address from device if needed
    // struct virtio_net_config *cfg = (struct virtio_net_config *)R(VIRTIO_MMIO_CONFIG);
    // for (int i = 0; i < 6; i++) net.mac[i] = cfg->mac[i];

    // Step 12: Mark driver as ready
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
}
void lwip_init_network() {
    printf("Initializing lwIP network stack...\n");
    lwip_init();
    printf("lwIP initialized.\n");

    ip4_addr_t ipaddr, netmask, gw;
    //ip_addr_t gw_ip;
    ip4addr_aton("10.0.2.15", &ipaddr);
    ip4addr_aton("255.255.255.0", &netmask);
    ip4addr_aton("10.0.2.2", &gw);
    
    //IP_ADDR4(&gw_ip, 10, 0, 2, 2); // Gateway IP address
    // Static ARP entry for gateway
    struct eth_addr qemu_mac = { .addr = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 } };
    etharp_add_static_entry(&gw, &qemu_mac);

    ip_addr_t ip_addr_any, netmask_ip, gw_ip;
    ip_addr_copy_from_ip4(ip_addr_any, ipaddr);
    ip_addr_copy_from_ip4(netmask_ip, netmask);
    ip_addr_copy_from_ip4(gw_ip, gw);

    printf("Setting up lwIP network interface...\n");
    netif_add(&lwip_netif, &ipaddr, &netmask, &gw,
              NULL, ethernetif_init, netif_input);
    printf("lwIP network interface added: %s\n", lwip_netif.name);
    netif_set_default(&lwip_netif);
    printf("Setting lwIP network interface up...\n");
    netif_set_up(&lwip_netif);
    printf("lwIP network interface is up.\n");
    printf("netif ip: %s\n", ip4addr_ntoa(&ipaddr));
}
