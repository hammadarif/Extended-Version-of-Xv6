#include "user.h"
#include "kernel/types.h"
#include "kernel/stat.h"

struct icmp_hdr {
    uint8 type;
    uint8 code;
    uint16 checksum;
    uint16 id;
    uint16 seq;
};

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
        exit(0);
    }

    char *dest_ip = argv[1];

    char packet[64];
    struct icmp_hdr *icmp = (struct icmp_hdr *)packet;
    memset(packet, 0, sizeof(packet));

    icmp->type = 8; // Echo request
    icmp->code = 0;
    icmp->id = 1234;
    icmp->seq = 1;
    icmp->checksum = checksum(packet, sizeof(packet));

    if (send_packet(packet, sizeof(packet)) < 0) {
        printf("Error: Failed to send packet\n");
        exit(1);
    }

    printf("Ping request sent to %s\n", dest_ip);

    char recv_buf[256];
    int len = recv_packet(recv_buf, sizeof(recv_buf));
    if (len < 0) {
        printf("Error: Failed to receive packet\n");
        exit(1);
    }

    struct icmp_hdr *reply = (struct icmp_hdr *)recv_buf;
    if (reply->type == 0 && reply->id == 1234) {
        printf("Ping reply received: seq=%d, len=%d\n", reply->seq, len);
    } else {
        printf("Unexpected reply or error\n");
    }

    exit(0);
}
