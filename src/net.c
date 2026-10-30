#include "net.h"

#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/altcp_tls.h"
#include "lwip/apps/http_client.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "roots.h"
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

// mbedTLS's clock (MBEDTLS_PLATFORM_MS_TIME_ALT): time since boot is all it needs
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return to_ms_since_boot(get_absolute_time());
}

// -- GETs (lwIP's HTTP client; its callbacks run in lwIP's context), one per slot

typedef struct {
    httpc_connection_t settings;
    char *buf;
    size_t cap;
    volatile size_t len;
    volatile int http_status;
    volatile net_get_status_t status;
    volatile bool overflow;
} get_slot_t;

static get_slot_t slots[NET_SLOTS];

// TLS: one client configuration (the trusted roots), and an allocator that also gives each
// connection the server's name, which Cloudflare needs (SNI) and lwIP's HTTP client doesn't set
static struct altcp_tls_config *tls_config;
static char tls_host[64];

static struct altcp_pcb *tls_alloc(void *arg, u8_t ip_type) {
    (void)arg;
    struct altcp_pcb *pcb = altcp_tls_alloc(tls_config, ip_type);
    if (pcb) mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(pcb), tls_host);
    return pcb;
}

static altcp_allocator_t tls_allocator = {tls_alloc, NULL};

static err_t on_body(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {
    (void)err;
    get_slot_t *s = arg;
    if (!p) return ERR_OK;
    size_t room = s->cap - 1 - s->len;
    size_t n = p->tot_len < room ? p->tot_len : room;
    if (n < p->tot_len) s->overflow = true;
    pbuf_copy_partial(p, s->buf + s->len, n, 0);
    s->len += n;
    altcp_recved(conn, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_done(void *arg, httpc_result_t result, u32_t rx_content_len, u32_t srv_res, err_t err) {
    (void)rx_content_len; (void)err;
    get_slot_t *s = arg;
    s->buf[s->len] = 0;
    s->http_status = srv_res;
    s->status = result == HTTPC_RESULT_OK && srv_res == 200 && !s->overflow ? NET_GET_DONE : NET_GET_FAILED;
}

bool net_get_start(int slot, const char *host, uint16_t port, bool tls, const char *path, char *buf, size_t cap) {
    get_slot_t *s = &slots[slot];
    if (s->status == NET_GET_BUSY || !net_up() || cap < 2) return false;
    if (tls) {
        // one TLS connection at a time: the allocator's host name is shared
        for (int i = 0; i < NET_SLOTS; i++)
            if (i != slot && slots[i].status == NET_GET_BUSY && slots[i].settings.altcp_allocator) return false;
        if (!tls_config) {
            cyw43_arch_lwip_begin();
            tls_config = altcp_tls_create_config_client((const u8_t *)roots_pem, roots_pem_len);
            cyw43_arch_lwip_end();
            if (!tls_config) return false;
        }
        snprintf(tls_host, sizeof tls_host, "%s", host);
    }
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->http_status = 0;
    s->overflow = false;
    s->status = NET_GET_BUSY;
    memset(&s->settings, 0, sizeof s->settings);
    s->settings.result_fn = on_done;
    s->settings.altcp_allocator = tls ? &tls_allocator : NULL;
    httpc_state_t *conn;
    cyw43_arch_lwip_begin();
    err_t e = httpc_get_file_dns(host, port, path, &s->settings, on_body, s, &conn);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        s->status = NET_GET_IDLE;
        return false;
    }
    return true;
}

net_get_status_t net_get_poll(int slot, size_t *len, int *http_status) {
    get_slot_t *s = &slots[slot];
    net_get_status_t st = s->status;
    if (st == NET_GET_DONE || st == NET_GET_FAILED) {
        if (len) *len = s->len;
        if (http_status) *http_status = s->http_status;
        s->status = NET_GET_IDLE;
    }
    return st;
}
