#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "virtio.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

// Size of the actual packet data (not counting virtio_net_hdr).
//#define PACKET_SIZE  2048

// Number of Rx/Tx slots
//#define NUM  8

struct virtio_net net;
struct netif lwip_netif;

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
static int
alloc_desc(struct virtqueue *q)
{
    for (int i = 0; i < 2 * NUM; i++) {
        if (q->free[i]) {
            q->free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void
free_desc(struct virtqueue *q, int idx)
{
    if (q->free[idx])
        panic("free_desc: already free");
    q->free[idx] = 1;
}

/* ----------------------------------------------------------------------
 *  Initialize a virtqueue
 * ---------------------------------------------------------------------- */
void
init_virtqueue(struct virtqueue *q, int qidx)
{
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_SEL, qidx);

    uint32 max = mmio_read(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max < NUM)
        panic("virtqueue too small");

    // Allocate descriptor, avail ring, used ring
    q->desc = kalloc();
    q->avail = kalloc();
    q->used = kalloc();

    if (!q->desc || !q->avail || !q->used)
        panic("virtqueue kalloc failed");

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

    // Mark all desc free, allocate buffers
    for (int i = 0; i < 2 * NUM; i++) {
        q->free[i] = 1;
        q->buf[i]  = kalloc();
        if (!q->buf[i])
            panic("virtqueue buf kalloc failed");
        memset(q->buf[i], 0, PGSIZE);
    }

    q->used_idx = 0;
}

/* ----------------------------------------------------------------------
 *  fill_rx: put a chain of 2 descriptors into the avail ring
 *    desc[2*i]   -> virtio_net_hdr at start of buffer
 *    desc[2*i+1] -> actual packet data following the header
 * ---------------------------------------------------------------------- */
static void
fill_rx(int i)
{
    // We'll use the i-th buffer in net.rx.buf[i] to hold:
    //  [virtio_net_hdr][packet data...]
    void *buf = net.rx.buf[i];

    // desc[2*i]: net header
    net.rx.desc[2*i].addr  = (uint64) buf;
    net.rx.desc[2*i].len   = sizeof(struct virtio_net_hdr);
    net.rx.desc[2*i].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
    net.rx.desc[2*i].next  = 2*i + 1;

    // desc[2*i+1]: remainder for actual packet data
    net.rx.desc[2*i+1].addr  = (uint64)((char*)buf + sizeof(struct virtio_net_hdr));
    net.rx.desc[2*i+1].len   = PACKET_SIZE - sizeof(struct virtio_net_hdr);
    net.rx.desc[2*i+1].flags = VRING_DESC_F_WRITE;
    net.rx.desc[2*i+1].next  = 0;

    // Add descriptor chain index (2*i) to the avail ring
    net.rx.avail->ring[ net.rx.avail->idx % NUM ] = 2*i;
    __sync_synchronize();
    net.rx.avail->idx++;

    // Notify the device (queue 0 for Rx)
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

/* ----------------------------------------------------------------------
 *  virtio_net_init
 * ---------------------------------------------------------------------- */
void
virtio_net_init()
{
    uint32 status = 0;

    initlock(&net.rx_lock, "virtio_net_rx");
    initlock(&net.tx_lock, "virtio_net_tx");

    // Check virtio version
    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION) != 2 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID) != 1) {
        panic("not a virtio net device");
    }

    // Reset device
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, 0);
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_CONFIG_S_DRIVER;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    // Feature negotiation
    uint32 features = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_FEATURES);
    // e.g. disable csum offload
    features &= ~(1 << VIRTIO_NET_F_CSUM);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_FEATURES, features);

    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);

    if (!(mmio_read(VIRTIO1 + VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio net: features ok fail");

    // init queues
    init_virtqueue(&net.rx, 0);
    init_virtqueue(&net.tx, 1);

    // fill Rx queue with descriptors
    for (int i = 0; i < NUM; i++) {
        fill_rx(i);
    }

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
}

/* ----------------------------------------------------------------------
 *  TX: send a packet
 * ---------------------------------------------------------------------- */
void
virtio_send_packet(void *data, uint len)
{
    acquire(&net.tx_lock);

    // If queue is "full"
    if (net.tx.avail->idx - net.tx.used_idx == NUM) {
        panic("virtio_send_packet: tx queue full");
    }

    int desc_idx = alloc_desc(&net.tx);
    void *buf    = net.tx.buf[desc_idx];

    memmove(buf, data, len);

    net.tx.desc[desc_idx].addr  = (uint64)buf;
    net.tx.desc[desc_idx].len   = len;
    net.tx.desc[desc_idx].flags = 0;
    net.tx.desc[desc_idx].next  = 0;

    net.tx.avail->ring[ net.tx.avail->idx % NUM ] = desc_idx;
    __sync_synchronize();
    net.tx.avail->idx++;

    // Notify device that there's a TX packet
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 1);

    release(&net.tx_lock);
}

/* ----------------------------------------------------------------------
 *  RX: retrieve one packet
 *  This version copies the *packet data* (minus virtio_net_hdr)
 *  into 'buffer'. *received_len is the packet's payload length.
 * ---------------------------------------------------------------------- */
void
virtio_receive_packet(char *buffer, int buflen, int *received_len)
{
    acquire(&net.rx_lock);

    // Check if any used ring entries are available
    if (net.rx.used->idx == net.rx.used_idx) {
        // No packets available
        *received_len = 0;
        release(&net.rx_lock);
        return;
    }

    // Which ring entry?
    int used_i    = net.rx.used_idx % NUM;
    int desc_idx  = net.rx.used->ring[used_i].id;  // e.g. 2*i
    uint len      = net.rx.used->ring[used_i].len; // total length device wrote

    // The device wrote 'len' bytes across our chain [virtio_net_hdr + data].
    // The net header is in the first sizeof(...) bytes, data follows.

    int i = desc_idx / 2; // which buffer slot
    char *buf = (char*) net.rx.buf[i];
    // skip the net header
    char *pkt_data = buf + sizeof(struct virtio_net_hdr);

    // The payload length is (len - sizeof(struct virtio_net_hdr)), if len is bigger.
    // But device might set 'len' to exactly how many bytes it wrote (including net hdr).
    int payload_len = (len > sizeof(struct virtio_net_hdr))
                    ? (len - sizeof(struct virtio_net_hdr)) : 0;

    if (payload_len > buflen) {
        // Packet doesn't fit in caller's buffer
        *received_len = -1;
        release(&net.rx_lock);
        return;
    }

    // Copy only the actual packet data to user buffer
    memmove(buffer, pkt_data, payload_len);
    *received_len = payload_len;

    // Now re-post this buffer to the Rx queue for future packets
    fill_rx(i);

    // Move forward in the used ring
    net.rx.used_idx++;

    release(&net.rx_lock);
}

/* ----------------------------------------------------------------------
 *  virtio_net_intr: handle interrupts
 * ---------------------------------------------------------------------- */
void
virtio_net_intr()
{
    acquire(&net.rx_lock);

    // Read the interrupt status
    uint32 irq_status = mmio_read(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS);

    // Check if the RX interrupt is triggered
    if (!(irq_status & VIRTIO_NET_INTR_RX)) {
        // If no RX interrupt, skip RX processing
        release(&net.rx_lock);
        return;
    }

    // If we have new used descriptors on RX side
    if (net.rx.used->idx > net.rx.used_idx) {
        // Typically we might wake up a sleep() or net thread
        wakeup(&net.rx); // or some other logic
    }
    // ----- TX side -----
    // Instead of incrementing net.tx.used_idx by 1,
    // loop through all newly completed descriptors:
    while (net.tx.used->idx > net.tx.used_idx) {
        int used_i = net.tx.used_idx % NUM;
        int desc_id = net.tx.used->ring[used_i].id;

        // Free the TX descriptor
        free_desc(&net.tx, desc_id);

        net.tx.used_idx++;
    }

    // Acknowledge the interrupt
    //uint32 irq_status = mmio_read(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_ACK, irq_status);

    release(&net.rx_lock);
}

/* ----------------------------------------------------------------------
 *  lwIP integration stubs (unchanged)
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

/*  Example handler to pass a pbuf to lwIP (not fully integrated):
static void virtio_lwip_input(struct pbuf *p) {
   struct pbuf *p = pbuf_alloc(PBUF_RAW, *received_len, PBUF_POOL);
    if (p) {
    memcpy(p->payload, buffer, *received_len);
    if (lwip_netif.input(p, &lwip_netif) != ERR_OK) {
        printf("ERROR: lwIP failed to process the packet.\n");
        pbuf_free(p);
    }
    }
}
*/
