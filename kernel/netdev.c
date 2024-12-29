#include "types.h"
#include "fs.h"
#include "file.h"
#include "virtio.h"

#define MAX_NET_DEVICES 4        // Maximum number of network devices
#define PACKET_SIZE 2048         // Maximum Ethernet frame size

// Structure to hold device-specific information
struct net_device {
    struct virtio_net *net;      // Pointer to the VirtIO network structure
    int dev_id;                 // Device ID
};

// Array to hold all registered network devices
struct net_device net_devices[MAX_NET_DEVICES];
int net_device_count = 0;        // Counter for registered devices

void virtio_send_packet(void *data, uint len);
void virtio_receive_packet(char *buffer, int buflen, int *received_len);
void* memmove(void*, const void*, uint);

// Function to read packets from a specific network device
int netdev_read(int fd, uint64 dst, int n) {
    int dev_id = fd - DEV_NET; // Calculate device ID from file descriptor
    if (dev_id < 0 || dev_id >= net_device_count) {
        return -1; // Invalid device ID
    }

    char buffer[PACKET_SIZE];
    int received_len = 0;

    virtio_receive_packet(buffer, sizeof(buffer), &received_len);

    if (received_len < 0) {
        return 0; // No packet or error
    }

    int len = (received_len < n) ? received_len : n;
    memmove((char *)dst, buffer, len); // Adjusted dst to char* for compatibility
    return len;
}

// Function to send packets to a specific network device
int netdev_write(int fd, uint64 src, int n) {
    int dev_id = fd - DEV_NET; // Calculate device ID from file descriptor
    if (dev_id < 0 || dev_id >= net_device_count) {
        return -1; // Invalid device ID
    }

    virtio_send_packet((void *)src, n); // Adjusted src to void* for compatibility
    return n;
}

// Function to initialize the network device table
void dev_net_init() {
    for (int i = 0; i < net_device_count; i++) {
        devsw[DEV_NET + i].read = netdev_read;
        devsw[DEV_NET + i].write = netdev_write;
    }
}

// Function to register a new network device
int register_net_device(struct virtio_net *new_net) {
    if (net_device_count >= MAX_NET_DEVICES) {
        return -1; // Too many devices
    }

    struct net_device *dev = &net_devices[net_device_count];
    dev->net = new_net;
    dev->dev_id = net_device_count;

    net_device_count++;
    return 0; // Success
}
