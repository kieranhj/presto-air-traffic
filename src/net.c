#include "net.h"

#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/apps/http_client.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#define REJOIN_AFTER_MS 15000   // a join that hasn't come up by then is tried again
#define SETTLE_MS       3000    // and none is judged sooner than this

static const char *wifi_ssid, *wifi_password;
static absolute_time_t joined_at;
static char ip_text[16];

bool net_init(const char *ssid, const char *password) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_UK)) return false;
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);     // no power saving: steady latency for polling
    wifi_ssid = ssid;
    wifi_password = password;
    cyw43_arch_wifi_connect_async(ssid, password, password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    joined_at = get_absolute_time();
    return true;
}

static int link_status(void) {
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
}

void net_poll(void) {
    int s = link_status();
    if (s == CYW43_LINK_UP) {
        if (!ip_text[0]) {
            cyw43_arch_lwip_begin();
            ip4addr_ntoa_r(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]), ip_text, sizeof ip_text);
            cyw43_arch_lwip_end();
            printf("net: up, %s\n", ip_text);
        }
        return;
    }
    ip_text[0] = 0;
    int64_t since_ms = absolute_time_diff_us(joined_at, get_absolute_time()) / 1000;
    if (since_ms < SETTLE_MS) return;   // a join reads as down for a moment before it starts
    bool failed = s == CYW43_LINK_FAIL || s == CYW43_LINK_NONET || s == CYW43_LINK_BADAUTH || s == CYW43_LINK_DOWN;
    if (failed || since_ms > REJOIN_AFTER_MS) {
        printf("net: %s, joining again\n", net_state());
        cyw43_arch_wifi_connect_async(wifi_ssid, wifi_password,
                                      wifi_password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
        joined_at = get_absolute_time();
    }
}

bool net_up(void) {
    return link_status() == CYW43_LINK_UP;
}

const char *net_state(void) {
    switch (link_status()) {
        case CYW43_LINK_UP: return "up";
        case CYW43_LINK_JOIN: return "joining";
        case CYW43_LINK_NOIP: return "getting an address";
        case CYW43_LINK_FAIL: return "failed";
        case CYW43_LINK_NONET: return "network not found";
        case CYW43_LINK_BADAUTH: return "wrong password";
        default: return "down";
    }
}

const char *net_ip(void) {
    return ip_text;
}

int net_rssi(void) {
    int32_t rssi = 0;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return rssi;
}

// -- GET (lwIP's HTTP client; its callbacks run in lwIP's context)

static httpc_connection_t settings;
static char *get_buf;
static size_t get_cap;
static volatile size_t get_len;
static volatile int get_http_status;
static volatile net_get_status_t get_status = NET_GET_IDLE;
static volatile bool get_overflow;

static err_t on_body(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) return ERR_OK;
    size_t room = get_cap - 1 - get_len;
    size_t n = p->tot_len < room ? p->tot_len : room;
    if (n < p->tot_len) get_overflow = true;
    pbuf_copy_partial(p, get_buf + get_len, n, 0);
    get_len += n;
    altcp_recved(conn, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_done(void *arg, httpc_result_t result, u32_t rx_content_len, u32_t srv_res, err_t err) {
    (void)arg; (void)rx_content_len; (void)err;
    get_buf[get_len] = 0;
    get_http_status = srv_res;
    get_status = result == HTTPC_RESULT_OK && srv_res == 200 && !get_overflow ? NET_GET_DONE : NET_GET_FAILED;
}

bool net_get_start(const char *host, uint16_t port, const char *path, char *buf, size_t cap) {
    if (get_status == NET_GET_BUSY || !net_up() || cap < 2) return false;
    get_buf = buf;
    get_cap = cap;
    get_len = 0;
    get_http_status = 0;
    get_overflow = false;
    get_status = NET_GET_BUSY;
    settings.use_proxy = 0;
    settings.result_fn = on_done;
    settings.headers_done_fn = NULL;
    httpc_state_t *conn;
    cyw43_arch_lwip_begin();
    err_t e = httpc_get_file_dns(host, port, path, &settings, on_body, NULL, &conn);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        get_status = NET_GET_IDLE;
        return false;
    }
    return true;
}

net_get_status_t net_get_poll(size_t *len, int *http_status) {
    net_get_status_t s = get_status;
    if (s == NET_GET_DONE || s == NET_GET_FAILED) {
        if (len) *len = get_len;
        if (http_status) *http_status = get_http_status;
        get_status = NET_GET_IDLE;
    }
    return s;
}
