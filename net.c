
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
static bool ntp_triggered = false;

static struct pbuf *received_frame;

extern void set_ntp_callback(uint32_t hour, uint32_t minute);

// NTP相关变量
static struct udp_pcb *ntp_pcb = NULL;
static volatile bool ntp_requested = false;

// NTP常量
#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define NTP_TIMESTAMP_DELTA 2208988800ULL

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void) netif;
    for (;;) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
    return etharp_output(netif, p, addr);
}

static err_t our_netif_init(struct netif *netif) {
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->state = NULL;
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
    
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, tud_network_mac_address, 6);
    netif->hwaddr[5] ^= 0x01;

    return ERR_OK;
}

void tud_network_init_cb(void) {
    if (received_frame) {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (received_frame) return false;
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            memcpy(p->payload, src, size);
            received_frame = p;
        }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *)ref;
    (void)arg;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

static void send_ntp_request(const ip_addr_t *addr) {
    if (!ntp_pcb) return;

    unsigned char ntp_packet[NTP_PACKET_SIZE] = {0};
    ntp_packet[0] = 0xE3;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_SIZE, PBUF_RAM);
    if (p) {
        memcpy(p->payload, ntp_packet, NTP_PACKET_SIZE);
        udp_sendto(ntp_pcb, p, addr, NTP_PORT);
        pbuf_free(p);
    }
}

static void dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr) {
        send_ntp_request(ipaddr);
    }
}

static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p && p->len >= NTP_PACKET_SIZE) {
        uint8_t *data = (uint8_t *)p->payload;
        uint32_t ntp_time = (data[40] << 24) | (data[41] << 16) | (data[42] << 8) | data[43];
        time_t epoch_time = ntp_time - NTP_TIMESTAMP_DELTA;
        struct tm *utc_time = gmtime(&epoch_time);

        if (utc_time) {
            current_hour = utc_time->tm_hour + 8;
            if (current_hour >= 24) current_hour -= 24;
            current_minute = utc_time->tm_min;
            time_synced = true;
            set_ntp_callback(current_hour, current_minute);
        }
    }
    if (p) pbuf_free(p);
}

static void service_traffic(void) {
    if (received_frame) {
        if (ethernet_input(received_frame, &netif) != ERR_OK) {
            pbuf_free(received_frame);
        }
        received_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}

void net_init(void) {
    lwip_init();

    netif_add(&netif, NULL, NULL, NULL, NULL, our_netif_init, ethernet_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    dhcp_start(&netif);

    ntp_pcb = udp_new();
    if (ntp_pcb) {
        udp_recv(ntp_pcb, ntp_recv, NULL);
    }
}

void net_request_ntp(void) {
    if (!ntp_requested) {
        ntp_requested = true;
        dns_gethostbyname("pool.ntp.org", NULL, dns_found, NULL);
    }
}

void net_task(void) {
    tud_task();
    service_traffic();
    
    if (netif_is_link_up(&netif) && dhcp_supplied_address(&netif)) {
        if (!network_connected) {
            network_connected = true;
        }
        
        if (!ntp_triggered) {
            ntp_triggered = true;
            net_request_ntp();
        }
    }
}

bool net_is_connected(void) {
    return network_connected;
}

bool net_time_synced(void) {
    return time_synced;
}
