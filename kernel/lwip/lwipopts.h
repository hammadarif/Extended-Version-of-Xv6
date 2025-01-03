#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Core configurations
#define NO_SYS                  0       // Enable threaded mode (required for tcpip_input)
#define MEM_ALIGNMENT           4       // Align memory to 4 bytes (suitable for RISC-V)
#define MEM_SIZE                (32 * 1024) // Increase memory pool size for reliability

// Protocols
#define LWIP_ARP                1       // Enable ARP
#define LWIP_IPV4               1       // Enable IPv4
#define LWIP_ICMP               1       // Enable ICMP (required for ping)
#define LWIP_TCP                1       // Disable TCP (not needed for ping)
#define LWIP_UDP                1       // Disable UDP (not needed for ping)

// PBUF configurations
#define PBUF_POOL_SIZE          16      // Number of pbufs in the pool
#define PBUF_POOL_BUFSIZE       1518    // Buffer size to hold a full Ethernet frame

// Optional APIs
#define LWIP_NETIF_API          1       // Enable network interface API
#define LWIP_NETCONN            1       // Disable Netconn API (optional)
#define LWIP_SOCKET             0       // Disable BSD-style socket API (optional)



#define LWIP_STATS              1       // Enable lwIP statistics
#define LWIP_ETHERNET           1       // Enable Ethernet support

// Debugging (Optional)
#define LWIP_DEBUG              1       // Enable lwIP debug options
#define ICMP_DEBUG              LWIP_DBG_ON
#define ETHARP_DEBUG            LWIP_DBG_ON
#define ETHARP_DEBUG LWIP_DBG_ON

#define LWIP_PROVIDE_ERRNO 1



#endif /* LWIPOPTS_H */
