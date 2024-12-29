#include "types.h"
//#include "defs.h"
#include "fs.h"
#include "file.h"
//#include "virtio.h"


void virtio_send_packet(void *data, uint len);
void virtio_receive_packet(char *buffer, int buflen, int *received_len);
void* memmove(void*, const void*, uint);
#define PACKET_SIZE 2048 // Maximum Ethernet frame size



int netdev_read(struct inode *ip, char *dst, int n) {
    char buffer[PACKET_SIZE];
    int received_len = 0;  // We'll let virtio_receive_packet fill this

    // Now we call with all three arguments
    virtio_receive_packet(buffer, sizeof(buffer), &received_len);

    // Check for error
    if (received_len < 0) {
        // e.g. packet too large or no packet available
        return 0; // or -1, depending on your design
    }

    // Otherwise, 'received_len' is how many bytes we actually got.
    int len = (received_len < n) ? received_len : n;
    memmove(dst, buffer, len);
    return len; // return the number of bytes copied
}

int netdev_write(struct inode *ip, char *src, int n) {
    virtio_send_packet(src, n);
    return n;
}

void dev_net_init() {
    devsw[DEV_NET].read = netdev_read;
    devsw[DEV_NET].write = netdev_write;
}