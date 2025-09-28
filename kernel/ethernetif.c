#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "virtio.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/tcp.h"
//#include "defs.h"
#include "lwip/etharp.h"
#include "lwip/ip.h"
#include "netif/ethernet.h"

// Current default mac address could be replaced by logic later on
static const uint8_t default_mac_address[ETH_HWADDR_LEN] = {0x02, 0xAB, 0x00, 0x00, 0x00, 0x01};
int virtio_send_packet(void *data, uint len);
void* memcpy(void *dst, const void *src, uint n);
#define MAX_PACKET_SIZE 1500
#define htons(x) ((((x)&0xff)<<8) | (((x)>>8)&0xff))
#define ntohs(x) htons(x)




err_t virtio_linkoutput(struct netif *netif, struct pbuf *p) {
    if (!p) return ERR_ARG;
    int len = p->tot_len;
    if (len <= 0 || len > MAX_PACKET_SIZE) return ERR_MEM;

    uint8_t buffer[MAX_PACKET_SIZE];
    if (pbuf_copy_partial(p, buffer, len, 0) != len)
        return ERR_IF;

    // DO NOT printf here; this path can run in interrupt context.
    if (virtio_send_packet(buffer, len) < 0)
        return ERR_IF;

    return ERR_OK;
}

/*
 * Initializes the Ethernet interface for lwIP and links it to the VirtIO driver.
 */


err_t ethernetif_input(struct netif *netif, void *data, int len) {
    if (len <= 0) {
        printf("[ERROR] ethernetif_input: Incoming data has no length\n");
        return ERR_VAL;
    }

    struct pbuf *p = pbuf_alloc(PBUF_LINK, len, PBUF_POOL);
    if (p == NULL) {
        printf("[ERROR] ethernetif_input: pbuf_alloc failed (len=%d)\n", len);
        return ERR_MEM; // No memory available
    }

    printf("Received packet of size %d\n", len);
    printf("RX first 16 bytes: ");
    for (int i = 0; i < 16 && i < len; i++)
        printf("%x ", ((uint8_t*)data)[i]);
    printf("\n");
    printf("RX: dst_mac=%x:%x:%x:%x:%x:%x, ethertype=0x%x\n",
           ((uint8_t*)data)[0], ((uint8_t*)data)[1], ((uint8_t*)data)[2],
           ((uint8_t*)data)[3], ((uint8_t*)data)[4], ((uint8_t*)data)[5],
           (((uint8_t*)data)[12] << 8) | ((uint8_t*)data)[13]);
    printf("\n");

    // Safe copy into the pbuf chain
    if (pbuf_take(p, data, len) != ERR_OK) {
        printf("[ERROR] ethernetif_input: pbuf_take failed\n");
        pbuf_free(p);
        return ERR_MEM;
    }

    // Debug: print IP/TCP headers if this is an IP packet
    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    if (ethhdr->type == PP_HTONS(ETHTYPE_IP)) {
        struct ip_hdr *iphdr = (struct ip_hdr *)((u8_t *)ethhdr + SIZEOF_ETH_HDR);
        ip4_addr_t src_ip, dst_ip;
        memcpy(&src_ip, &iphdr->src, sizeof(ip4_addr_t));
        memcpy(&dst_ip, &iphdr->dest, sizeof(ip4_addr_t));
        printf("IP packet: src=%s dst=%s proto=%d\n",
               ip4addr_ntoa(&src_ip), ip4addr_ntoa(&dst_ip), IPH_PROTO(iphdr));

        if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
            struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)iphdr + IPH_HL(iphdr) * 4);
            printf("TCP packet: srcport=%d dstport=%d flags=0x%x\n",
                   ntohs(tcphdr->src), ntohs(tcphdr->dest), TCPH_FLAGS(tcphdr));
        }
    }

    // Hand packet up to lwIP
    err_t res = netif->input(p, netif);
    if (res != ERR_OK) {
        printf("[ERROR] ethernetif_input: netif->input returned %d\n", res);
        pbuf_free(p);  // free pbuf if lwIP didn’t consume it
    }

    return res;
}


err_t ethernetif_init(struct netif *netif) {
    // Set interface name
    netif->name[0] = 'e';
    netif->name[1] = 'n';

    // Set hardware address length
    netif->hwaddr_len = ETH_HWADDR_LEN; // 6 for Ethernet

    // Set hardware address (MAC)
    memcpy(netif->hwaddr, default_mac_address, ETH_HWADDR_LEN);

    // Set MTU (Maximum Transmission Unit)
    netif->mtu = MAX_PACKET_SIZE; // Standard Ethernet MTU

    // Set interface capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    // Set output functions
    netif->output = etharp_output;          // Handle ARP requests
    netif->linkoutput = virtio_linkoutput; // Send packets via VirtIO
    netif->input = ethernet_input;

    return ERR_OK;
}
