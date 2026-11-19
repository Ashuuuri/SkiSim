#include "net_client.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <string.h>

#include "protocol.h"

// How long one join attempt may take before we start a new one
#define JOIN_TIMEOUT_MS   10000
// How often the link state is checked
#define POLL_INTERVAL_MS  250
// Wait this long before retrying a failed join (avoids a log flood)
#define JOIN_RETRY_MS     1000

// Stall detection. When the server's AP disappears (e.g. the server is
// re-flashed) the wireless chip can wedge: every send then blocks for about
// a second and fails, while the network interface still looks "up".
#define SEND_SLOW_US            50000   // a healthy send takes well under 1 ms
#define SEND_FAILS_TO_REJOIN    3       // failed or slow sends in a row
#define LINK_DOWN_POLLS         2       // radio says "not joined" this many polls
#define REJOINS_BEFORE_REBOOT   3       // forced rejoins with no good send between

static bool joining = false;
static bool connected = false;
static absolute_time_t join_started;
static absolute_time_t next_poll;
static absolute_time_t join_retry_at;

static int  send_fails_in_row = 0;
static bool rejoin_requested = false;
static int  link_down_polls = 0;
static int  rejoins_without_success = 0;

static struct udp_pcb *data_pcb;
static ip_addr_t server_addr;

static const char *link_status_name(int status) {
    switch (status) {
        case CYW43_LINK_DOWN:    return "down";
        case CYW43_LINK_JOIN:    return "joining";
        case CYW43_LINK_NOIP:    return "joined, no IP";
        case CYW43_LINK_UP:      return "up";
        case CYW43_LINK_FAIL:    return "failed";
        case CYW43_LINK_NONET:   return "AP not found";
        case CYW43_LINK_BADAUTH: return "bad password";
        default:                 return "unknown";
    }
}

// Give the station interface its static address. With LWIP_DHCP=0 the
// driver leaves it at 0.0.0.0, and the link never reports "up" until set.
static void set_static_ip(void) {
    ip4_addr_t ip, mask, gw;
    ip4addr_aton(SKI_CLIENT_IP, &ip);
    ip4addr_aton(SKI_NETMASK, &mask);
    ip4addr_aton(SKI_SERVER_IP, &gw);

    cyw43_arch_lwip_begin();
    netif_set_addr(&cyw43_state.netif[CYW43_ITF_STA], &ip, &mask, &gw);
    cyw43_arch_lwip_end();
}

static void start_join(void) {
    printf("[net] joining \"%s\" ...\n", SKI_WIFI_SSID);
    int err = cyw43_arch_wifi_connect_async(SKI_WIFI_SSID, SKI_WIFI_PASSWORD,
                                            CYW43_AUTH_WPA2_AES_PSK);
    if (err) {
        printf("[net] join request failed (%d)\n", err);
    }
    joining = true;
    join_started = get_absolute_time();
}

// Drop the association and rejoin from scratch. If that keeps failing to
// produce a single good send, reboot: a fresh chip always recovers.
static void force_rejoin(const char *reason) {
    connected = false;
    joining = false;
    send_fails_in_row = 0;
    link_down_polls = 0;
    if (++rejoins_without_success > REJOINS_BEFORE_REBOOT) {
        printf("[net] %s; still stuck after %d rejoins, rebooting\n", reason,
               REJOINS_BEFORE_REBOOT);
        sleep_ms(50);                       // let the message out
        watchdog_reboot(0, 0, 0);
        while (true) tight_loop_contents();
    }
    printf("[net] %s; leaving and rejoining (%d/%d)\n", reason,
           rejoins_without_success, REJOINS_BEFORE_REBOOT);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    join_retry_at = make_timeout_time_ms(500);
}

int net_client_init(void) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("[net] wireless chip init failed\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    set_static_ip();

    ipaddr_aton(SKI_SERVER_IP, &server_addr);
    cyw43_arch_lwip_begin();
    data_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    cyw43_arch_lwip_end();
    if (!data_pcb) {
        printf("[net] could not allocate UDP pcb\n");
        return 2;
    }

    next_poll = get_absolute_time();
    join_retry_at = get_absolute_time();
    start_join();
    return 0;
}

void net_client_poll(void) {
    if (absolute_time_diff_us(get_absolute_time(), next_poll) > 0) {
        return;
    }
    next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

    if (rejoin_requested) {
        rejoin_requested = false;
        if (connected) {
            force_rejoin("sends failing or stalled");
            return;
        }
    }

    cyw43_arch_lwip_begin();
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();

    if (status == CYW43_LINK_UP) {
        // The netif can stay "up" after the AP is gone; ask the radio too.
        if (connected) {
            int radio = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
            link_down_polls = (radio == CYW43_LINK_JOIN) ? 0 : link_down_polls + 1;
            if (link_down_polls >= LINK_DOWN_POLLS) {
                force_rejoin("radio no longer joined");
                return;
            }
        }
        if (!connected) {
            connected = true;
            joining = false;
#if SKI_WIFI_POWERSAVE_OFF
            // Applied after every join, since joining can restore the default.
            cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
#endif
            printf("[net] connected: IP %s, server %s, power save %s\n",
                   SKI_CLIENT_IP, SKI_SERVER_IP,
                   SKI_WIFI_POWERSAVE_OFF ? "off" : "on");
        }
        return;
    }

    if (connected) {
        connected = false;
        printf("[net] link lost (%s)\n", link_status_name(status));
        start_join();
        return;
    }

    if (joining) {
        bool failed = (status == CYW43_LINK_FAIL || status == CYW43_LINK_NONET ||
                       status == CYW43_LINK_BADAUTH);
        bool timed_out = absolute_time_diff_us(join_started, get_absolute_time())
                         > (int64_t)JOIN_TIMEOUT_MS * 1000;
        if (failed || timed_out) {
            printf("[net] join %s (%s), retrying in %d ms\n",
                   timed_out ? "timed out" : "failed", link_status_name(status),
                   JOIN_RETRY_MS);
            joining = false;
            join_retry_at = make_timeout_time_ms(JOIN_RETRY_MS);
        }
    } else if (absolute_time_diff_us(get_absolute_time(), join_retry_at) <= 0) {
        start_join();
    }
}

bool net_client_is_connected(void) {
    return connected;
}

bool net_client_send(const void *data, uint16_t len) {
    if (!connected) return false;

    absolute_time_t t0 = get_absolute_time();
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    err_t err = ERR_MEM;
    if (p) {
        memcpy(p->payload, data, len);
        err = udp_sendto(data_pcb, p, &server_addr, SKI_PORT_DATA);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();

    bool slow = absolute_time_diff_us(t0, get_absolute_time()) > SEND_SLOW_US;
    if (err == ERR_OK && !slow) {
        send_fails_in_row = 0;
        rejoins_without_success = 0;
    } else if (++send_fails_in_row >= SEND_FAILS_TO_REJOIN) {
        rejoin_requested = true;            // handled in the next poll
    }
    return err == ERR_OK;
}
