#include "types.h"
#include "defs.h"
#include "fs.h"
#include "file.h"
#include "virtio.h"

int netdev_read(struct inode *ip, char *dst, int n) {
    char buffer[PACKET_SIZE];
    virtio_receive_packet(buffer, sizeof(buffer));

    int len = MIN(n, PACKET_SIZE);
    memmove(dst, buffer, len);
    return len;
}

int netdev_write(struct inode *ip, char *src, int n) {
    virtio_send_packet(src, n);
    return n;
}

void dev_net_init() {
    devsw[DEV_NET].read = netdev_read;
    devsw[DEV_NET].write = netdev_write;
}