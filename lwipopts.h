
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_ALIGNMENT               4
#define MEM_SIZE                    64000
#define MEMP_NUM_PBUF               64
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_NETBUF             16
#define MEMP_NUM_NETCONN            4
#define MEMP_NUM_SYS_TIMEOUT        8

#define PBUF_POOL_SIZE              64
#define PBUF_POOL_BUFSIZE           2000

#define LWIP_DHCP                   1
#define LWIP_DNS                    1

#define LWIP_CHKSUM_ALGORITHM       3

#define LWIP_ICMP                   1
#define LWIP_IGMP                   0
#define LWIP_UDP                    1
#define LWIP_TCP                    1

#define TCP_MSS                     1460
#define TCP_WND                     (8*TCP_MSS)
#define TCP_SND_BUF                 (8*TCP_MSS)
#define TCP_QUEUE_OOSEQ             0
#define TCP_SND_QUEUELEN            16
#define TCP_SNDQUEUELOWAT           8

#define LWIP_DISABLE_TCP_SANITY_CHECKS 1

#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK     1

#define LWIP_RAW                    0

#endif /* __LWIPOPTS_H__ */
