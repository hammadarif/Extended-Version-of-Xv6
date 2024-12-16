#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "virtio.h"
#include "defs.h"
#include "lwip/etharp.h"

// Replace this with your MAC address initialization logic
static const uint8_t default_mac_address[ETH_HWADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

/*
 * Initializes the Ethernet interface for lwIP and links it to the VirtIO driver.
 */
err_t ethernetif_init(struct netif *netif) {
    // Set interface name
    netif->name[0] = 'e';
    netif->name[1] = 'n';

    // Set hardware address length
    netif->hwaddr_len = ETH_HWADDR_LEN; // 6 for Ethernet

    // Set hardware address (MAC)
    memcpy(netif->hwaddr, default_mac_address, ETH_HWADDR_LEN);

    // Set MTU (Maximum Transmission Unit)
    netif->mtu = 1500; // Standard Ethernet MTU

    // Set interface capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    // Set output functions
    netif->output = etharp_output;          // Handle ARP requests
    netif->linkoutput = virtio_send_packet; // Send packets via VirtIO

    return ERR_OK;
}
