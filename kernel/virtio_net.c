#include "types.h"
#include "riscv.h"
#include "defs.h"
//#include "spinlock.h"
//#include "memlayout.h"
#include "virtio.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"


#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

struct virtio_net net;

struct netif lwip_netif;


// Helper functions for MMIO register access
uint32 mmio_read(uint64 addr) {
    return *(volatile uint32 *)(addr);
}

void mmio_write(uint64 addr, uint32 val) {
    *(volatile uint32 *)(addr) = val;
}

/* 
 * Allocates a free descriptor from the Virtqueue
 * Returns the descriptor index or -1 if no descriptors are available.
 */
static int alloc_desc(struct virtqueue *q) {
    for (int i = 0; i < 2 * NUM; i++) {
        if (q->free[i]) {
            q->free[i] = 0;
            return i;
        }
    }
    return -1; // No free descriptor available
}


/*
 * Frees a descriptor in the Virtqueue, marking it as available.
 */
static void free_desc(struct virtqueue *q, int idx) {
    if (q->free[idx]) 
        panic("Descriptor already free");
    q->free[idx] = 1;
}

/* 
 * Initializes a Virtqueue.
 * - Allocates memory for descriptor tables, available/used rings.
 * - Sets up pointers for the queue in the device.
 * - Initializes the bookkeeping structures (free descriptors, buffer pool).
 */
void init_virtqueue(struct virtqueue *q, int qidx) {
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_SEL, qidx);

    uint32 max = mmio_read(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max < NUM) panic("Virtqueue size too small");

    // Allocate and clear memory for Virtqueue structures
    q->desc = kalloc();
    q->avail = kalloc();
    q->used = kalloc();

    if (!q->desc || !q->avail || !q->used)
        panic("Virtqueue allocation failed");

    memset(q->desc, 0, PGSIZE);
    memset(q->avail, 0, PGSIZE);
    memset(q->used, 0, PGSIZE);

    // Configure queue size and addresses
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NUM, NUM);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_LOW, (uint64)q->desc);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint64)q->desc >> 32);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_LOW, (uint64)q->avail);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_HIGH, (uint64)q->avail >> 32);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_LOW, (uint64)q->used);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_HIGH, (uint64)q->used >> 32);

    // Mark the queue as ready
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_READY, 1);

    // Initialize descriptor and buffer pool
    for (int i = 0; i < 2 * NUM; i++) {
        q->free[i] = 1;
        q->buf[i] = kalloc();
        if (!q->buf[i]) panic("Buffer allocation failed");
        memset(q->buf[i], 0, PGSIZE);
    }

    q->used_idx = 0;
}

/* 
 * Prepares RX descriptors and notifies the device.
 * - Each descriptor points to a buffer where the device writes incoming packets.
 */
static void fill_rx(int i) {
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)net.rx.buf[i];

    // Configure RX descriptor chain
    net.rx.desc[2 * i].addr = (uint64)hdr;
    net.rx.desc[2 * i].len = sizeof(struct virtio_net_hdr);
    net.rx.desc[2 * i].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
    net.rx.desc[2 * i].next = 2 * i + 1;

    net.rx.desc[2 * i + 1].addr = (uint64)net.rx.buf[i];
    net.rx.desc[2 * i + 1].len = PACKET_SIZE;
    net.rx.desc[2 * i + 1].flags = VRING_DESC_F_WRITE;
    net.rx.desc[2 * i + 1].next = 0;

    // Add to available ring
    net.rx.avail->ring[net.rx.avail->idx % (2 * NUM)] = 2 * i;
    __sync_synchronize();
    net.rx.avail->idx++;

    // Notify device
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

void cleanup_rx_descriptor(struct virtqueue *q, int idx) {
    free_desc(q, idx);
}

/* 
 * VirtIO-Net initialization function.
 * - Verifies device properties.
 * - Negotiates features and initializes RX/TX queues.
 */
void virtio_net_init() {
    uint32 status = 0;

    initlock(&net.rx_lock, "virtio_net_rx");
    initlock(&net.tx_lock, "virtio_net_tx");

    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION) != 2 ||
        mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID) != 1)
        panic("Not a VirtIO network device");

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

    if (!(mmio_read(VIRTIO1 + VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("Feature negotiation failed");

    // Initialize RX and TX queues
    init_virtqueue(&net.rx, 0);
    init_virtqueue(&net.tx, 1);

    // Fill RX queue
    for (int i = 0; i < NUM; i++)
        fill_rx(i);

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, status);
}

/* 
 * Sends a packet by adding it to the TX queue.
 */
void virtio_send_packet(void *data, uint len) {
    acquire(&net.tx_lock);

    if (net.tx.avail->idx - net.tx.used_idx == NUM)
        panic("TX queue full");

    int desc_idx = alloc_desc(&net.tx);
    void *buf = net.tx.buf[desc_idx];

    memmove(buf, data, len);

    net.tx.desc[desc_idx].addr = (uint64)buf;
    net.tx.desc[desc_idx].len = len;
    net.tx.desc[desc_idx].flags = 0;
    net.tx.desc[desc_idx].next = 0;

    net.tx.avail->ring[net.tx.avail->idx % NUM] = desc_idx;
    __sync_synchronize();
    net.tx.avail->idx++;

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 1);

    release(&net.tx_lock);
}

/* 
 * Processes received packets from the RX queue.
 */
void virtio_receive_packet(char *buffer, int buflen, int *received_len) {
    acquire(&net.rx_lock);

    // Ensure there are packets to process
    if (net.rx.used->idx == net.rx.used_idx) {
        *received_len = 0; // No packets available
        release(&net.rx_lock);
        return;
    }

    // Process the RX descriptor
    int desc_idx = net.rx.used->ring[net.rx.used_idx % NUM].id; // Descriptor ID
    uint len = net.rx.used->ring[net.rx.used_idx % NUM].len;    // Length of received data

    // Check if the received data fits in the provided buffer
    if (len > buflen) {
        *received_len = -1; // Indicate error (packet too large)
        release(&net.rx_lock);
        return;
    }

    // Copy data from the VirtIO buffer to the user-provided buffer
    memmove(buffer, net.rx.buf[desc_idx], len);

    // Update received length
    *received_len = len;

    // Refill the RX descriptor for future use
    fill_rx(desc_idx / 2);

    // Increment used index
    net.rx.used_idx++;

    release(&net.rx_lock);
}
/* 
 * Handles interrupts for the VirtIO-Net device.
 */
void virtio_net_intr() {
    acquire(&net.rx_lock);

    if (net.rx.used->idx > net.rx.used_idx) {
        // Process RX interrupts
        wakeup(&net.rx);
    }

    if (net.tx.used->idx > net.tx.used_idx) {
        // Process TX interrupts
        net.tx.used_idx++;
    }

    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_ACK,
               mmio_read(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS));

    release(&net.rx_lock);
}

//lwip integration to virtio_net
/*Function to init lwip basic functions*/
void lwip_init_network() {
    lwip_init(); // Initialize the lwIP stack

    // IP address configuration
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 1, 2);    // Set static IP (xv6 machine's IP)
    IP4_ADDR(&netmask, 255, 255, 255, 0); // Set subnet mask
    IP4_ADDR(&gw, 192, 168, 1, 1);        // Set gateway (e.g., router IP)

    // Add the network interface to lwIP
    netif_add(&lwip_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&lwip_netif); // Set as the default network interface
    netif_set_up(&lwip_netif);      // Bring up the network interface
}

static void virtio_lwip_input(struct pbuf *p) __attribute__((unused));
static void virtio_lwip_input(struct pbuf *p) {
    if (p != NULL) {
        if (lwip_netif.input(p, &lwip_netif) != ERR_OK) {
            printf("ERROR: lwIP failed to process the packet.\n");
            pbuf_free(p);
        }
    }
}
