// Shared network settings and packet format for the SkiSim client and server.
// This is the single place where SSID, IP addresses, and ports are defined.
#ifndef SKISIM_PROTOCOL_H
#define SKISIM_PROTOCOL_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// WiFi access point (run by the server)
// ---------------------------------------------------------------------------
#define SKI_WIFI_SSID      "skisim"
#define SKI_WIFI_PASSWORD  "skisim-5730"   // WPA2 needs at least 8 characters

// Static addressing; the server runs no DHCP server.
#define SKI_SERVER_IP      "192.168.4.1"
#define SKI_CLIENT_IP      "192.168.4.2"
#define SKI_NETMASK        "255.255.255.0"

// UDP ports
#define SKI_PORT_DATA      4210   // client -> server, sensor packets
#define SKI_PORT_PING      4211   // both directions, latency measurement

// WiFi power saving adds large, bursty latency. Set to 0 only to measure
// the "before" case for the latency comparison (Section 8, step 6).
#define SKI_WIFI_POWERSAVE_OFF 1

// ---------------------------------------------------------------------------
// Data packet (client -> server), little-endian
// ---------------------------------------------------------------------------
#define SKI_PKT_MAGIC      0x5A4B
#define SKI_PKT_VERSION    1

#define SKI_UNIT_BOARD     1
#define SKI_UNIT_CHEST     2      // reserved for the future chest unit

#define SKI_FLAG_GYRO_CAL  (1u << 0)   // gyro bias calibration done
#define SKI_FLAG_IMU_ERR   (1u << 1)   // IMU read error

typedef struct __attribute__((packed)) {
    uint16_t magic;     // SKI_PKT_MAGIC
    uint8_t  version;   // SKI_PKT_VERSION
    uint8_t  unit_id;   // SKI_UNIT_*
    uint32_t seq;       // +1 on every send
    uint32_t t_us;      // client time_us_32() at send time
    int32_t  angle;     // tilt angle, fix15, degrees
    int32_t  rate;      // angular rate, fix15, degrees/second
    uint16_t flags;     // SKI_FLAG_*
} ski_packet_t;

_Static_assert(sizeof(ski_packet_t) == 22, "ski_packet_t must be 22 bytes");

#endif // SKISIM_PROTOCOL_H
