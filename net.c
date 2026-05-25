
#include "pico/stdlib.h"
#include "pico/time.h"
#include "lwipopts.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "netif/etharp.h"
#include "tusb.h"
#include "rndis_protocol.h"
#include "dhserver.h"
#include "dnserver.h"
#include <string.h>
#include <stdio.h>

static struct netif netif;
static uint32_t current_hour = 0;
static uint32_t current_minute = 0;
static bool time_synced = false;
static bool network_connected = false;

static struct pbuf *received_frame;

extern void set_ntp_callback(uint32_t hour, uint32_t minute);
extern void indicate_network_status(uint8_t status);

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }
static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

#define MAX_DHCP_CLIENTS 1
static dhcp_entry_t entries[MAX_DHCP_CLIENTS] = { { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 } };
static dhcp_config_t dhcp_config = { .router = INIT_IP4(0, 0, 0, 0), .port = 67, .dns = INIT_IP4(0, 0, 0, 0), .domain = NULL, .num_entry = MAX_DHCP_CLIENTS, .entries = entries };

static bool dns_query_proc(const char *name, ip4_addr_t *addr)
{
    return false;
}

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    for (;;)
    {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len))
        {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    return etharp_output(netif, p, addr);
}

static err_t our_netif_init(struct netif *netif)
{
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

void tud_network_init_cb(void)
{
    if (received_frame)
    {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (received_frame) return false;
    if (size)
    {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p)
        {
            memcpy(p->payload, src, size);
            received_frame = p;
        }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    struct pbuf *p = (struct pbuf *)ref;
    (void)arg;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

static void service_traffic(void)
{
    if (received_frame)
    {
        if (ethernet_input(received_frame, &netif) != ERR_OK)
        {
            pbuf_free(received_frame);
        }
        received_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}

static struct tcp_pcb *http_server_pcb = NULL;

const char html_header[] = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";

const char html_body_start[] =
    "<!DOCTYPE html><html>"
    "<head><meta charset='UTF-8'><title>LED Clock</title></head>"
    "<body style='font-family:Arial,sans-serif;padding:40px;max-width:500px;margin:0 auto;'>"
    "<h1 style='color:#333;text-align:center;'>LED Clock - Time Setup</h1>"
    "<div style='background:#f5f5f5;padding:30px;border-radius:10px;margin-top:20px;'>"
    "<form method='POST' action='/set'>";

const char html_hour_field[] =
    "<div style='margin-bottom:20px;'><label style='display:block;margin-bottom:8px;font-weight:bold;'>Hour (0-23):</label>"
    "<input type='number' name='hour' id='h' min='0' max='23' autocomplete='off' style='width:100%;padding:10px;font-size:16px;border:2px solid #ddd;border-radius:5px;'><script>document.getElementById('h').value=new Date().getHours();</script></div>";

const char html_minute_field[] =
    "<div style='margin-bottom:20px;'><label style='display:block;margin-bottom:8px;font-weight:bold;'>Minute (0-59):</label>"
    "<input type='number' name='minute' id='m' min='0' max='59' autocomplete='off' style='width:100%;padding:10px;font-size:16px;border:2px solid #ddd;border-radius:5px;'><script>document.getElementById('m').value=new Date().getMinutes();</script></div>";

const char html_end[] =
    "<button type='submit' style='width:100%;padding:15px;font-size:18px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;font-weight:bold;'>Set Time</button>"
    "</form></div>"
    "<div style='margin-top:30px;text-align:center;color:#666;'><p>Device Address: 192.168.7.1</p><p>First set static IP on PC to 192.168.7.2</p></div>"
    "</body></html>";

static void generate_html_page(char *buffer, size_t buffer_size)
{
    char *ptr = buffer;
    
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_header);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_body_start);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_hour_field);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_minute_field);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_end);
}

const char success_body[] =
    "<!DOCTYPE html><html>"
    "<head><meta charset='UTF-8'><title>Success!</title></head>"
    "<body style='font-family:Arial,sans-serif;padding:40px;text-align:center;'>"
    "<h1 style='color:#4CAF50;'>Time Set Successfully!</h1>"
    "<div style='background:#e8f5e9;padding:30px;border-radius:10px;margin-top:20px;display:inline-block;'>"
    "<p style='font-size:24px;margin:0;'>Current Time: ";

const char success_end[] =
    "</p></div>"
    "<p style='margin-top:30px;'><a href='/' style='color:#2196F3;font-size:18px;'>Back to Setup</a></p>"
    "</body></html>";

static void generate_success_page(char *buffer, size_t buffer_size, int hour, int minute)
{
    char *ptr = buffer;
    
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", html_header);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", success_body);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%02d:%02d", hour, minute);
    ptr += snprintf(ptr, buffer_size - (ptr - buffer), "%s", success_end);
}

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg;
    (void)len;
    tcp_close(tpcb);
    return ERR_OK;
}

static int parse_post_data(const char *data, int *hour, int *minute)
{
    const char *hour_param = strstr(data, "hour=");
    const char *minute_param = strstr(data, "minute=");
    
    if (hour_param && minute_param) {
        *hour = atoi(hour_param + 5);
        *minute = atoi(minute_param + 7);
        return 1;
    }
    return 0;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }
    
    char *request = (char *)p->payload;
    static char response[4096];
    
    if (strstr(request, "POST /set") != NULL) {
        int hour = 0, minute = 0;
        if (parse_post_data(request, &hour, &minute)) {
            if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
                current_hour = hour;
                current_minute = minute;
                time_synced = true;
                indicate_network_status(3);
                set_ntp_callback(hour, minute);
                generate_success_page(response, sizeof(response), hour, minute);
            } else {
                generate_html_page(response, sizeof(response));
            }
        } else {
            generate_html_page(response, sizeof(response));
        }
    } else {
        generate_html_page(response, sizeof(response));
    }
    
    err_t err_write = tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    if (err_write == ERR_OK) {
        tcp_output(tpcb);
    }
    tcp_sent(tpcb, http_sent);
    
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    (void)err;
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

void net_init(void)
{
    lwip_init();

    netif_add(&netif, &ipaddr, &netmask, &gateway, NULL, our_netif_init, ethernet_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    
    indicate_network_status(1);
    
    http_server_pcb = tcp_new();
    if (http_server_pcb) {
        ip4_addr_t any_addr;
        IP4_ADDR(&any_addr, 0, 0, 0, 0);
        tcp_bind(http_server_pcb, &any_addr, 80);
        http_server_pcb = tcp_listen(http_server_pcb);
        tcp_accept(http_server_pcb, http_accept);
    }
    
    dhserv_init(&dhcp_config);
    dnserv_init(&ipaddr, 53, dns_query_proc);
}

void net_task(void)
{
    tud_task();
    service_traffic();
    
    if (netif_is_link_up(&netif) && !network_connected)
    {
        network_connected = true;
        indicate_network_status(2);
    }
}

bool net_is_connected(void)
{
    return network_connected;
}

bool net_time_synced(void)
{
    return time_synced;
}

