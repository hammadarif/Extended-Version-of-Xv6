#include "types.h"
#include "fs.h"
#include "file.h"
#include "virtio.h"
#include "proc.h"


//#define PACKET_SIZE 1500         // Maximum Ethernet frame size
#define MAX_NET_DEVICES 8        // Define maximum devices

struct net_device {
    struct virtio_net *net;
    int dev_id;
    int active;
};

extern void printf(char *fmt, ...);

struct net_device net_devices[MAX_NET_DEVICES];
int net_device_count = 0;

int virtio_send_packet(void *data, uint len);
void virtio_receive_packet(char *buffer, int buflen, int *received_len);
void free_virtio_net(struct virtio_net *net);
void *memmove(void *, const void *, uint);

uint64 walkaddr(pagetable_t pagetable, uint64 va);
struct proc* myproc();

struct virtio_net *init_virtio_net(int dev_id);

int netdev_read(int fd, uint64 dst, int n) {
    int dev_id = fd - DEV_NET;
    //printf("[NETDEV]The device id : %d\n", dev_id);

    if (dev_id < 0 || dev_id >= MAX_NET_DEVICES) {
        printf("[NETDEV]Invalid device ID: %d\n", dev_id);
        return -1;
    }

    struct net_device *dev = &net_devices[dev_id];
    if (!dev->active) {
        printf("[NETDEV]Device %d is not active\n", dev_id);
        return -1;
    }

    char buffer[PACKET_SIZE];
    int received_len = 0;

    virtio_receive_packet(buffer, sizeof(buffer), &received_len);
    if (received_len <= 0) {
        printf("[NETDEV]No packets available\n");
        return 0; // No data received
    }

    char *kernel_dst = (char *)walkaddr(myproc()->pagetable, dst);
    if (!kernel_dst) {
        printf("[NETDEV]Invalid user address: %p\n", (char *)dst);
        return -1;
    }

    int len = (received_len < n) ? received_len : n;
    memmove(kernel_dst, buffer, len);

    if (len < received_len) {
        printf("[NETDEV]Truncated packet from %d to %d bytes\n", received_len, len);
    }

    return len; // Number of bytes copied to the user buffer
}

int netdev_write(int fd, uint64 src, int n) {
    int dev_id = fd - DEV_NET;
    if (dev_id < 0 || dev_id >= MAX_NET_DEVICES) return -1;

    struct net_device *dev = &net_devices[dev_id];
    if (!dev->active) return -1;

    if (!src || n <= 0) {
        printf("Invalid src or size: src=%p, n=%d\n", (char *)src, n);
        return -1;
    }

    if ((uint64)src % 8 != 0) {
        printf("Unaligned src pointer: %p\n", (char *)src);
        return -1;
    }

     // Translate user-space address to kernel-space address
    char *kernel_src = (char *)walkaddr(myproc()->pagetable, src);
    if (!kernel_src) {
        printf("Invalid user-space address: %p\n", (char *)src);
        return -1;
    }

    if (n > PACKET_SIZE) {
        printf("Truncating write size from %d to %d\n", n, PACKET_SIZE);
        n = PACKET_SIZE;
    }
    char buffer[PACKET_SIZE];
    memmove(buffer, kernel_src, n);
    if(virtio_send_packet(buffer, n) < 0)
    {
        printf("Error: virtio_send_packet failed\n");
        return -1;
    }

    return n;
}

int register_net_device(struct virtio_net *net, int dev_id) {
    if (dev_id < 0 || dev_id >= MAX_NET_DEVICES) {
        printf("Invalid device ID: %d\n", dev_id);
        return -1;
    }

    struct net_device *dev = &net_devices[dev_id];
    if (dev->active) {
        printf("Device already active: %d\n", dev_id);
        return -1;
    }

    if (!net) {
        printf("Invalid VirtIO network device for ID: %d\n", dev_id);
        return -1;
    }

    dev->net = net;
    dev->active = 1;

    printf("Successfully registered VirtIO network device: %d\n", dev_id);
    net_device_count++;
    return 0;
}

void init_netdev() {
    //for (int i = 0; i < MAX_NET_DEVICES; i++) {
        struct virtio_net *net = init_virtio_net(0);
        if (net) {
            register_net_device(net, 0);
        }
    //}

    devsw[DEV_NET].read = netdev_read;
    devsw[DEV_NET].write = netdev_write;
}
unsigned long
r_mtime(void)
{
  return *(uint64*)CLINT_MTIME;
}