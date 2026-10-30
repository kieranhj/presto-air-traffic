// lwIP for the Presto: no OS (the SDK's threadsafe-background mode), IPv4, DHCP, DNS, TCP for
// HTTP GETs, and TLS (mbedTLS, src/mbedtls_config.h) for the route look-ups. Sized for an
// aircraft.json of up to ~100 KB arriving at Wi-Fi speed.
#pragma once

// The heap (and with it mbedTLS's buffers, about 40 KB a connection, which lwIP's TLS layer
// allocates from it) lives in PSRAM, after the two screen buffers: main.c sets PSRAM up before
// the network. It isn't touched by DMA (the CYW43 driver copies what it sends), so the cache
// keeps it coherent.
#define LWIP_RAM_HEAP_POINTER       ((void *)0x11080000)

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (192 * 1024)
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_TCP_KEEPALIVE          0   // (with LWIP_ALTCP, lwIP's proxy-connect app won't build with it)
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_CHKSUM_ALGORITHM       3

// TLS for the Worker's route look-ups: the certificate must check out against src/roots.c
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              1
#define LWIP_ALTCP_TLS_MBEDTLS      1
#define ALTCP_MBEDTLS_AUTHMODE      MBEDTLS_SSL_VERIFY_REQUIRED

#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0
#define LWIP_DEBUG                  0
