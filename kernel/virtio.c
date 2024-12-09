#include "types.h"
#include "virtio.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// Buffers for descriptors, available, and used rings
static struct virtq_desc virtq_desc[NUM];
static struct virtq_avail virtq_avail;
static struct virtq_used virtq_used;

// Index for the free descriptor ring
static uint16 free_desc_idx = 0;

// Packet buffers
#define PACKET_SIZE 1518 // Maximum Ethernet frame size
static char net_buffers[NUM][PACKET_SIZE];

// Helper functions for MMIO register access
static inline uint32 mmio_read(uint64 addr) {
    return *(volatile uint32 *)(addr);
}

static inline void mmio_write(uint64 addr, uint32 val) {
    *(volatile uint32 *)(addr) = val;
}

// Initialize the VirtIO networking device
void virtio_init() {
    //testing for virtnet device
    printf("VirtIO MMIO Base: 0x%x\n", VIRTIO1);

    uint32 magic = mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE);
    uint32 version = mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION);
    uint32 device_idd = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID);
    uint32 vendor_id = mmio_read(VIRTIO1 + VIRTIO_MMIO_VENDOR_ID);

    printf("VirtIO Magic: 0x%x, Version: %d, Device ID: %d, Vendor ID: 0x%x\n",
           magic, version, device_idd, vendor_id);

    uint32 versions = mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION);
    printf("VirtIO MMIO Version: %d\n", versions);

    if (magic != 0x74726976)
        panic("Invalid VirtIO device");
    if (device_idd != 1)
        panic("Not a VirtIO network device");

    // Step 1: Verify the device
    uint32 device_id = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID);
    printf("VirtIO DEVICE_ID: %d\n", device_id);
    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976)
        panic("Invalid VirtIO device");
    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_VERSION) != 2)
        panic("Unsupported VirtIO version");
    if (mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_ID) != 1)
        panic("Not a VirtIO network device");

    // Step 2: Reset the device
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, 0);

    // Step 3: Acknowledge and declare driver readiness
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_DRIVER);

    // Step 4: Negotiate features
    uint32 device_features = mmio_read(VIRTIO1 + VIRTIO_MMIO_DEVICE_FEATURES);
    uint32 driver_features = device_features; // Accept all available features for now
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_FEATURES, driver_features);

    // Step 5: Confirm features are accepted
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_FEATURES_OK);
    if (!(mmio_read(VIRTIO1 + VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("Device features not accepted");

    // Step 6: Set up descriptor and ring buffers
    uint64 desc_addr = (uint64)virtq_desc;
    uint64 avail_addr = (uint64)&virtq_avail;
    uint64 used_addr = (uint64)&virtq_used;

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_LOW, desc_addr & 0xFFFFFFFF);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_DESC_HIGH, desc_addr >> 32);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_LOW, avail_addr & 0xFFFFFFFF);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DRIVER_DESC_HIGH, avail_addr >> 32);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_LOW, used_addr & 0xFFFFFFFF);
    mmio_write(VIRTIO1 + VIRTIO_MMIO_DEVICE_DESC_HIGH, used_addr >> 32);

    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_READY, 1); // Mark queue as ready

    // Step 7: Set DRIVER_OK status
    mmio_write(VIRTIO1 + VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_DRIVER_OK);
}

// Allocate a free descriptor
static int alloc_desc() {
    if (free_desc_idx >= NUM)
        return -1; // No free descriptors
    return free_desc_idx++;
}

// Free a descriptor
static void free_desc(int idx) {
    virtq_desc[idx].addr = 0;
    virtq_desc[idx].len = 0;
    virtq_desc[idx].flags = 0;
    virtq_desc[idx].next = 0;
}

// Send a packet
void virtio_send_packet(void *data, uint len) {
    int desc_idx = alloc_desc();
    if (desc_idx == -1)
        panic("No free descriptor for sending");

    // Copy data into the buffer
    memmove(net_buffers[desc_idx], data, len);

    // Set up descriptor
    virtq_desc[desc_idx].addr = (uint64)net_buffers[desc_idx];
    virtq_desc[desc_idx].len = len;
    virtq_desc[desc_idx].flags = 0; // No chaining

    // Add descriptor to available ring
    virtq_avail.ring[virtq_avail.idx % NUM] = desc_idx;
    virtq_avail.idx++;

    // Notify the device
    mmio_write(VIRTIO1 + VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

// Receive a packet
void virtio_receive_packet(void *buffer, uint buflen) {
    if (virtq_used.idx == 0)
        return; // No packets received

    int desc_idx = virtq_used.ring[virtq_used.idx % NUM].id;
    uint32 len = virtq_used.ring[virtq_used.idx % NUM].len;

    if (len > buflen)
        panic("Buffer too small for received packet");

    // Copy packet data to the provided buffer
    memmove(buffer, net_buffers[desc_idx], len);

    // Free the descriptor
    free_desc(desc_idx);
}
