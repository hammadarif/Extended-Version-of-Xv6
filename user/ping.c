#include "user.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"

uint16
htons(uint16 x)
{
    return ((x & 0x00ffU) << 8) | ((x & 0xff00U) >> 8);
}
uint16
ntohs(uint16 x)
{
    return htons(x);
}
uint16 checksum(void *data, int len) {
    uint16 *buf = (uint16 *)data;
    uint32 sum = 0;

    for (int i = 0; i < len / 2; i++) {
        sum += buf[i];
    }

    if (len % 2 == 1) {
        sum += ((uint8 *)data)[len - 1];
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: ping <destination_ip>\n");
        exit(1);
    }

    // Open network device
    int fd = open("/dev/net", O_RDWR);
    if (fd < 0) {
        printf("Failed to open /dev/net\n");
        exit(1);
    }
    printf("Device opened successfully.\n");

    // Prepare ICMP packet
    char packet[64];
    struct icmp_hdr {
        uint8 type;
        uint8 code;
        uint16 checksum;
        uint16 id;
        uint16 seq;
    } *icmp = (struct icmp_hdr *)packet;

    memset(packet, 0, sizeof(packet));
    
    icmp->type = 8; // Echo request
    icmp->code = 0;
    icmp->id = htons(1234);
    icmp->seq = htons(1);
    icmp->checksum = 0;
    icmp->checksum = checksum(icmp, sizeof(struct icmp_hdr));
    
   

    // Send ICMP packet

    if (netdev_write(fd, (uint64)packet, sizeof(packet)) < 0) {
        printf("Error: Failed to send packet\n");
        close(fd);
        exit(1);
    }
    
    // Receive ICMP reply
    char recv_buf[256];
    int len = netdev_read(fd, (uint64)recv_buf, sizeof(recv_buf));
    printf("[PING] The value of len: %d \n",len);
    if (len < 0) {
        printf("[PING]Error: Failed to receive packet\n");
        close(fd);
        exit(1);
    }

    struct icmp_hdr *reply = (struct icmp_hdr *)recv_buf;
    printf("[PING]Ping reply received: seq=%d, len=%d\n", ntohs(reply->seq), len);
    if (reply->type == 0 && reply->id == htons(1234)) {
        printf("[PING]Ping reply received: seq=%d, len=%d\n", ntohs(reply->seq), len);
    } else {
        printf("[PING]Unexpected or invalid reply received\n");
    }

    close(fd);
    return 0;
}
