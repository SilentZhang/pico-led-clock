
#include "pico/stdlib.h"
#include "pico/time.h"
#include "lwipopts.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/init.h"
#include "netif/etharp.h"
#include "tusb.h"
#include "rndis_protocol.h"
#include <string.h>

static struct netif netif;
static uint32_t current_hour = 0;
static uint32_t current_minute = 0;
static bool time_synced = false;
static bool network_connected = false;

extern void set_ntp_callback(uint32_t hour, uint32_t minute);

// NTP相关变量
static struct udp_pcb *ntp_pcb = NULL;
static volatile bool ntp_requested = false;
static const ip_addr_t *ntp_server_ip = NULL;

// NTP常量
#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define NTP_TIMESTAMP_DELTA 2208988800ULL // 从1900年到1970年的秒数

// 发送NTP请求
static void send_ntp_request(const ip_addr_t *addr) {
    if (!ntp_pcb) return;

    unsigned char ntp_packet[NTP_PACKET_SIZE] = {0};
    ntp_packet[0] = 0xE3;
    ntp_packet[1] = 0x00;
    ntp_packet[2] = 0x06;
    ntp_packet[3] = 0xEC;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_SIZE, PBUF_RAM);
    if (p) {
        memcpy(p->payload, ntp_packet, NTP_PACKET_SIZE);
        udp_sendto(ntp_pcb, p, addr, NTP_PORT);
        pbuf_free(p);
    }
}

// 处理DNS响应
static void dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr) {
        ntp_server_ip = ipaddr;
        send_ntp_request(ipaddr);
    }
}

// 处理NTP响应
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p && p->len >= NTP_PACKET_SIZE) {
        uint8_t *data = (uint8_t *)p->payload;
        uint32_t ntp_time = (data[40] << 24) | (data[41] << 16) | (data[42] << 8) | data[43];
        time_t epoch_time = ntp_time - NTP_TIMESTAMP_DELTA;
        struct tm *utc_time = gmtime(&epoch_time);

        if (utc_time) {
            current_hour = utc_time->tm_hour + 8; // 转换为UTC+8
            if (current_hour >= 24) current_hour -= 24;
            current_minute = utc_time->tm_min;
            time_synced = true;
            set_ntp_callback(current_hour, current_minute);
        }
    }
    if (p) pbuf_free(p);
}

// 网络初始化
static err_t our_netif_init(struct netif *netif) {
    netif->hwaddr_len = 6;
    netif->hwaddr[0] = 0x02;
    netif->hwaddr[1] = 0x02;
    netif->hwaddr[2] = 0x84;
    netif->hwaddr[3] = 0x6A;
    netif->hwaddr[4] = 0x96;
    netif->hwaddr[5] = 0x00;

    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output = etharp_output;

    return ERR_OK;
}

// TinyUSB CDC-ECM 网络设备相关
void tud_network_init_cb(void) {
}

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p) {
        memcpy(p->payload, src, size);
        if (netif.input(p, &netif) != ERR_OK) {
            pbuf_free(p);
            return false;
        }
        return true;
    }
    return false;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *)ref;
    uint16_t len = 0;
    if (p) {
        len = pbuf_copy_partial(p, dst, p->len, 0);
    }
    return len;
}

// 初始化网络
void net_init(void) {
    lwip_init();
    netif_add(&netif, NULL, NULL, NULL, NULL, our_netif_init, ethernet_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    dhcp_start(&netif);

    // 初始化UDP连接用于NTP
    ntp_pcb = udp_new();
    if (ntp_pcb) {
        udp_recv(ntp_pcb, ntp_recv, NULL);
    }
}

// 请求NTP同步
void net_request_ntp(void) {
    if (!ntp_requested) {
        ntp_requested = true;
        dns_gethostbyname("pool.ntp.org", NULL, dns_found, NULL);
    }
}

// 网络任务处理
void net_task(void) {
    tud_task();
    sys_check_timeouts();
    if (netif_is_link_up(&netif) && dhcp_supplied_address(&netif) && !network_connected) {
        network_connected = true;
    }
}

bool net_is_connected(void) {
    return network_connected;
}

bool net_time_synced(void) {
    return time_synced;
}
