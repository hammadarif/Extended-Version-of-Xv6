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
//#define htons(x) ((((x)&0xff)<<8) | (((x)>>8)&0xff))
//#define ntohs(x) htons(x)



/*
 * Initializes the Ethernet interface for lwIP and links it to the VirtIO driver.
 */
/*
err_t virtio_linkoutput(struct netif *netif, struct pbuf *p) {
    printf("Transmitting packet of size %d\n", p->len);
    // Allocate a buffer for transmission
    if (p->len > MAX_PACKET_SIZE) {
        return ERR_MEM; // Return error if packet is too large
    }

    // Call the NIC's transmission function
     if (virtio_send_packet(p->payload, p->len) < 0) {
        printf("[ERROR] Failed to send packet using virtio_send_packet\n");
        return ERR_IF;
    }
    ;

    // Assume success for now (add error checks if needed)
    return ERR_OK;
}
*/
err_t virtio_linkoutput(struct netif *netif, struct pbuf *p) {
    printf("Transmitting packet of size %d\n", p->tot_len);
    if (!p) {
        printf("[ERROR] virtio_linkoutput called with NULL pbuf\n");
        return ERR_ARG;
    }

    if (p->tot_len <= 0 || p->tot_len > MAX_PACKET_SIZE) {
        printf("[ERROR] Invalid packet length in virtio_linkoutput: %d\n", p->tot_len);
        return ERR_MEM;
    }

    uint8_t buffer[MAX_PACKET_SIZE];
    if (pbuf_copy_partial(p, buffer, p->tot_len, 0) != p->tot_len) {
        printf("[ERROR] pbuf_copy_partial failed\n");
        return ERR_IF;
    }
    
    printf("TX first 14 bytes: ");
    for (int i = 0; i < 16 && i < p->tot_len; i++)
        printf("%x ", buffer[i] & 0xff);
    printf("\n");
    if (virtio_send_packet(buffer, p->tot_len) < 0) {
        printf("[ERROR] Failed to send packet using virtio_send_packet\n");
        return ERR_IF;
    }

    return ERR_OK;
}
err_t ethernetif_input(struct netif *netif, void *data, int len) {
    // Allocate a pbuf (lwIP's buffer abstraction)
    if (len <= 0)
    {
        printf("Incoming data has no length \n");
    }
    
    struct pbuf *p = pbuf_alloc(PBUF_LINK, len, PBUF_POOL);
    if (p == NULL) {
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
    (((uint8_t*)data)[12]<<8) | ((uint8_t*)data)[13]);
    printf("\n");
    // Copy the data into the pbuf

    memcpy(p->payload, data, len);
    
    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    if (ethhdr->type == PP_HTONS(ETHTYPE_IP)) {
        struct ip_hdr *iphdr = (struct ip_hdr *)((u8_t *)ethhdr + SIZEOF_ETH_HDR);
        ip4_addr_t src_ip, dst_ip;
        memcpy(&src_ip, &iphdr->src, sizeof(ip4_addr_t));
        memcpy(&dst_ip, &iphdr->dest, sizeof(ip4_addr_t));
        printf("IP packet: src=%s dst=%s proto=%d\n", ip4addr_ntoa(&src_ip), ip4addr_ntoa(&dst_ip), IPH_PROTO(iphdr));

        if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
            struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)iphdr + IPH_HL(iphdr) * 4);
            printf("TCP packet: srcport=%d dstport=%d flags=0x%x\n",
                   ntohs(tcphdr->src), ntohs(tcphdr->dest), TCPH_FLAGS(tcphdr));
        }
    }

    // Pass the pbuf to lwIP
    err_t res = netif->input(p, netif);
    if (res != ERR_OK) printf("ethernetif_input: netif->input returned %d\n", res);
    //pbuf_free(p); // Free the pbuf after processing
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
