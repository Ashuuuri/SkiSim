// lwIP configuration shared by the SkiSim client and server.
// Based on the course WiFi/UDP demo (lwipopts_examples_common.h), trimmed to
// what this project needs: UDP only, static IPs, no TCP/DHCP/DNS.
#ifndef SKISIM_LWIPOPTS_H
#define SKISIM_LWIPOPTS_H

// Bare-metal, no RTOS
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC             1
#else
#define MEM_LIBC_MALLOC             0   // must be 0 for threadsafe_background
#endif

// Memory
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define PBUF_POOL_SIZE              16
#define MEMP_NUM_ARP_QUEUE          10

// Protocols
#define LWIP_IPV4                   1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1   // lets a laptop ping the boards
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    0
#define LWIP_DHCP                   0   // both ends use static IPs (protocol.h)
#define LWIP_DNS                    0

// Network interface
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

// Statistics off
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#endif // SKISIM_LWIPOPTS_H
