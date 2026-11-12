#include "net_ap.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "protocol.h"

#define MAX_STATIONS 8

int net_ap_init(void) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("[net] wireless chip init failed\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode(SKI_WIFI_SSID, SKI_WIFI_PASSWORD,
                              CYW43_AUTH_WPA2_AES_PSK);

    // The driver already defaults the AP to 192.168.4.1; set it from
    // protocol.h anyway so the config header stays the single source.
    ip4_addr_t ip, mask, gw;
    ip4addr_aton(SKI_SERVER_IP, &ip);
    ip4addr_aton(SKI_NETMASK, &mask);
    ip4addr_aton(SKI_SERVER_IP, &gw);
    cyw43_arch_lwip_begin();
    netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &ip, &mask, &gw);
    cyw43_arch_lwip_end();

#if SKI_WIFI_POWERSAVE_OFF
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
#endif

    printf("[net] AP \"%s\" up, IP %s, power save %s\n", SKI_WIFI_SSID,
           SKI_SERVER_IP, SKI_WIFI_POWERSAVE_OFF ? "off" : "on");
    return 0;
}

int net_ap_station_count(void) {
    uint8_t macs[6 * MAX_STATIONS];
    int num = MAX_STATIONS;   // in: buffer capacity, out: stations found
    cyw43_wifi_ap_get_stas(&cyw43_state, &num, macs);
    return num;
}
