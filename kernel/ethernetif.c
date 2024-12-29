#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "virtio.h"
//#include "defs.h"
#include "lwip/etharp.h"

// Replace this with your MAC address initialization logic
static const uint8_t default_mac_address[ETH_HWADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
void virtio_send_packet(void *data, uint len);
void* memcpy(void *dst, const void *src, uint n);
#define MAX_PACKET_SIZE 1500

/*
 * Initializes the Ethernet interface for lwIP and links it to the VirtIO driver.
 */
err_t virtio_linkoutput(struct netif *netif, struct pbuf *p) {
    // Allocate a buffer for transmission
    if (p->len > MAX_PACKET_SIZE) {
        return ERR_MEM; // Return error if packet is too large
    }

    // Call the NIC's transmission function
    virtio_send_packet(p->payload, p->len);

    // Assume success for now (add error checks if needed)
    return ERR_OK;
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
