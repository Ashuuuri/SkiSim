// Sensor source over UDP (default, SKI_SOURCE=UDP).
// Receives ski_packet_t on SKI_PORT_DATA and keeps the latest sample per unit.
//
// The receive callback runs in the lwIP background context, so it only
// validates and copies. Readers wrap access in cyw43_arch_lwip_begin/end,
// which keeps the callback from running halfway through a copy.

#include "sensor_source.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "protocol.h"

// A sequence number this far below the last one means the client rebooted
// and restarted from 0, not that the packet is merely old.
#define SEQ_REBOOT_JUMP  1000

typedef struct {
    bool            have;     // at least one packet accepted
    sensor_sample_t sample;
    sensor_stats_t  stats;
} unit_state_t;

static struct udp_pcb *rx_pcb;
static unit_state_t units[SENSOR_MAX_UNITS];
static uint32_t invalid_count;

static void on_packet(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;
    if (!p) return;

    ski_packet_t pkt;
    bool ok = (p->tot_len == sizeof pkt) &&
              (pbuf_copy_partial(p, &pkt, sizeof pkt, 0) == sizeof pkt);
    pbuf_free(p);

    if (!ok || pkt.magic != SKI_PKT_MAGIC || pkt.version != SKI_PKT_VERSION ||
        pkt.unit_id == 0 || pkt.unit_id >= SENSOR_MAX_UNITS) {
        invalid_count++;
        return;
    }

    unit_state_t *u = &units[pkt.unit_id];
    uint32_t now = time_us_32();

    if (u->have) {
        bool timed_out = (now - u->sample.rx_us) > SENSOR_TIMEOUT_US;
        bool rebooted  = (pkt.seq < u->sample.seq) &&
                         (u->sample.seq - pkt.seq) > SEQ_REBOOT_JUMP;
        if (timed_out || rebooted) {
            u->stats.resets++;               // start tracking from this packet
        } else if (pkt.seq <= u->sample.seq) {
            u->stats.stale++;                // old or duplicate: drop
            return;
        } else {
            u->stats.gaps += pkt.seq - u->sample.seq - 1;
        }
    }

    u->have          = true;
    u->sample.valid  = true;
    u->sample.angle  = pkt.angle;
    u->sample.rate   = pkt.rate;
    u->sample.seq    = pkt.seq;
    u->sample.rx_us  = now;
    u->sample.flags  = pkt.flags;
    u->stats.received++;
}

int sensor_source_init(void) {
    cyw43_arch_lwip_begin();
    rx_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    err_t err = rx_pcb ? udp_bind(rx_pcb, IP_ANY_TYPE, SKI_PORT_DATA) : ERR_MEM;
    if (err == ERR_OK) udp_recv(rx_pcb, on_packet, NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("[udp] could not listen on port %d (err %d)\n", SKI_PORT_DATA, err);
        return 1;
    }
    printf("[udp] listening for sensor packets on port %d\n", SKI_PORT_DATA);
    return 0;
}

bool sensor_source_get(uint8_t unit_id, sensor_sample_t *out) {
    if (unit_id == 0 || unit_id >= SENSOR_MAX_UNITS) {
        out->valid = false;
        return false;
    }
    cyw43_arch_lwip_begin();
    *out = units[unit_id].sample;
    cyw43_arch_lwip_end();

    if (out->valid && (time_us_32() - out->rx_us) > SENSOR_TIMEOUT_US) {
        out->valid = false;
    }
    return out->valid;
}

void sensor_source_stats(uint8_t unit_id, sensor_stats_t *out) {
    memset(out, 0, sizeof *out);
    if (unit_id == 0 || unit_id >= SENSOR_MAX_UNITS) return;
    cyw43_arch_lwip_begin();
    *out = units[unit_id].stats;
    cyw43_arch_lwip_end();
}

uint32_t sensor_source_invalid_count(void) {
    return invalid_count;
}
