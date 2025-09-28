#ifndef VIRTIO_H
#define VIRTIO_H
//
// virtio device definitions.
// for both the mmio interface, and virtio descriptors.
// only tested with qemu.
//
// the virtio spec:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

#include "spinlock.h"
#include "memlayout.h"

// virtio mmio control registers, mapped starting at 0x10001000.
// from qemu virtio_mmio.h
#define VIRTIO_MMIO_MAGIC_VALUE		0x000 // 0x74726976
#define VIRTIO_MMIO_VERSION		0x004 // version; should be 2
#define VIRTIO_MMIO_DEVICE_ID		0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID		0x00c // 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_QUEUE_SEL		0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM		0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY		0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 // write-only
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 // write-only
#define VIRTIO_MMIO_STATUS		0x070 // read/write
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW	0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH	0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW	0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH	0x0a4
#define VIRTIO_MMIO_CONFIG              0x100 // configuration space
//#define PACKET_SIZE 1518

// status register bits, from qemu virtio_config.h
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
#define VIRTIO_CONFIG_S_DRIVER		2
#define VIRTIO_CONFIG_S_DRIVER_OK	4
#define VIRTIO_CONFIG_S_FEATURES_OK	8

// device feature bits
#define VIRTIO_BLK_F_RO              5	/* Disk is read-only */
#define VIRTIO_BLK_F_SCSI            7	/* Supports scsi command passthru */
#define VIRTIO_BLK_F_CONFIG_WCE     11	/* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ             12	/* support more than one vq */
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29

// this many virtio descriptors.
// must be a power of two.
//#define NUM 8

// Networking Header (VirtIO spec, 5.1.6.4)
#define VIRTIO_NET_HDR_F_NEEDS_CSUM  1 // Device needs checksum computation
#define VIRTIO_NET_HDR_F_DATA_VALID  2 // Device indicates data is valid
#define VIRTIO_NET_HDR_GSO_NONE      0 // No Generic Segmentation Offload (GSO)
#define VIRTIO_NET_HDR_GSO_TCPV4     1 // TCPv4 segmentation offload

// VirtIO networking-specific feature flags
#define VIRTIO_NET_F_MAC        (1 << 5)  // Device has a MAC address
#define VIRTIO_NET_F_STATUS     (1 << 16) // Link status is available
//#define VIRTIO_NET_F_CTRL_VQ    (1 << 17) // Control virtqueue
#define VIRTIO_NET_F_CTRL_VQ      17
#define VIRTIO_NET_F_CSUM (1 << 0) // Device handles checksum offloading

#define VIRTIO_NET_INTR_RX 0x1

#define MAX_NET_DEVICES 8        // Maximum number of network devices


#define NUM 8 // Number of descriptors, must be a power of two
#define PACKET_SIZE 1500 // Maximum Ethernet frame size

// Alignment requirements
#define VIRTIO_ALIGNMENT        4096     // 4 KB alignment

// a single descriptor, from the spec.
struct virtq_desc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
};
#define VRING_DESC_F_NEXT  1 // chained with another descriptor
#define VRING_DESC_F_WRITE 2 // device writes (vs read)

// the (entire) avail ring, from the spec.
struct virtq_avail {
  uint16 flags; // always zero
  uint16 idx;   // driver will write ring[idx] next
  uint16 ring[NUM]; // descriptor numbers of chain heads
  uint16 unused;
};

// one entry in the "used" ring, with which the
// device tells the driver about completed requests.
struct virtq_used_elem {
  uint32 id;   // index of start of completed descriptor chain
  uint32 len;
};

struct virtq_used {
  uint16 flags; // always zero
  uint16 idx;   // device increments when it adds a ring[] entry
  struct virtq_used_elem ring[NUM];
};

// these are specific to virtio block devices, e.g. disks,
// described in Section 5.2 of the spec.

#define VIRTIO_BLK_T_IN  0 // read the disk
#define VIRTIO_BLK_T_OUT 1 // write the disk

// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.
struct virtio_blk_req {
  uint32 type; // VIRTIO_BLK_T_IN or ..._OUT
  uint32 reserved;
  uint64 sector;
};

// VirtIO networking packet header structure
struct virtio_net_hdr {
    uint8 flags;        // Flags (e.g., needs checksum computation)
    uint8 gso_type;     // Type of GSO (Generic Segmentation Offload)
    uint16 hdr_len;     // Header length (bytes)
    uint16 gso_size;    // Maximum size of a GSO segment
    uint16 csum_start;  // Start of the checksum computation
    uint16 csum_offset; // Offset for the checksum field
};
struct virtio_net_config { 
  uint8 mac[6];                 // only valid if VIRTIO_NET_F_MAC is set
  uint16 status;                // only exists if VIRTIO_NET_F_STATUS is set
  uint16 max_virtqueue_pairs;   // only exists if VIRTIO_NET_F_MQ is set
  uint16 mtu;                   // only exists if VIRTIO_NET_F_MTU is set
};


struct virtqueue {
    struct virtq_desc *desc;     // Descriptor table for Virtqueue
    struct virtq_avail *avail;  // Available ring for descriptors to device
    struct virtq_used *used;    // Used ring for descriptors processed by device

    char free[2 * NUM];         // Array to track free descriptors
    uint16 used_idx;            // Tracks processed entries in the used ring
    void *buf[2 * NUM];         // Buffer pool for descriptor memory
};
// MMIO helper declarations
uint32 mmio_read(uint64 addr);
void mmio_write(uint64 addr, uint32 val);

// Expose VirtIO net structure for testing
struct virtio_net {
    struct virtqueue rx;         // Receive (RX) queue
    struct virtqueue tx;         // Transmit (TX) queue
    struct spinlock rx_lock;     // Lock for RX operations
    struct spinlock tx_lock;     // Lock for TX operations
};

// External declaration for the global VirtIO-Net instance
extern struct virtio_net net;
#endif // VIRTIO_H
void init_virtqueue(struct virtqueue *q, int qidx);


#define VIRTIO_F_VERSION_1             32
//#define VIRTIO_NET_F_MAC               5
#define VIRTIO_NET_F_GUEST_CSUM        0
#define VIRTIO_NET_F_GUEST_TSO4        7
#define VIRTIO_NET_F_GUEST_TSO6        8
#define VIRTIO_NET_F_HOST_TSO4         11
#define VIRTIO_NET_F_HOST_TSO6         12
#define VIRTIO_NET_F_MRG_RXBUF         15
#define VIRTIO_NET_F_CTRL_RX           18
#define VIRTIO_NET_F_CTRL_MAC_ADDR     23