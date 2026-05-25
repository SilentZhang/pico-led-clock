
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16000
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            2
#define MEMP_NUM_TCP_PCB_LISTEN     1
#define MEMP_NUM_TCP_SEG            8
#define MEMP_NUM_NETBUF             4
#define MEMP_NUM_NETCONN            4
#define MEMP_NUM_SYS_TIMEOUT        8

#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1600

#define LWIP_DHCP                   1
#define LWIP_DNS                    1

#define LWIP_CHKSUM_ALGORITHM       3

#define LWIP_ICMP                   1
#define LWIP_IGMP                   0
#define LWIP_UDP                    1
#define LWIP_TCP                    1

#define TCP_MSS                     1460
#define TCP_WND                     (2*TCP_MSS)
#define TCP_SND_BUF                 (2*TCP_MSS)
#define TCP_QUEUE_OOSEQ             0
#define TCP_SND_QUEUELEN            4
#define TCP_SNDQUEUELOWAT           2

#define LWIP_DISABLE_TCP_SANITY_CHECKS 1

#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK     1

#define LWIP_RAW                    0

#endif /* __LWIPOPTS_H__ */
