#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "virtio.h"
//#include "defs.h"
#include "lwip/etharp.h"

// Current default mac address could be replaced by logic later on
static const uint8_t default_mac_address[ETH_HWADDR_LEN] = {0x02, 0xAB, 0x00, 0x00, 0x00, 0x01};
int virtio_send_packet(void *data, uint len);
void* memcpy(void *dst, const void *src, uint n);
#define MAX_PACKET_SIZE 1500



/*
 * Initializes the Ethernet interface for lwIP and links it to the VirtIO driver.
 */
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

err_t ethernetif_input(struct netif *netif, void *data, int len) {
    // Allocate a pbuf (lwIP's buffer abstraction)
    if (len <= 0)
    {
        printf("Incoming data has no length \n");
    }
    
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p == NULL) {
        return ERR_MEM; // No memory available
    }

    // Copy the data into the pbuf
    memcpy(p->payload, data, len);

    // Pass the pbuf to lwIP
    return netif->input(p, netif);
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

    return ERR_OK;
}
