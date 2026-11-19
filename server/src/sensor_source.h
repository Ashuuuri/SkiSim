// Sensor source interface. The game asks this for the latest sample of a
// unit and never cares whether it arrived over UDP or from a local IMU.
// Implementations: source_udp.c (default), source_local.c (wired fallback).
#ifndef SKISIM_SENSOR_SOURCE_H
#define SKISIM_SENSOR_SOURCE_H

#include <stdbool.h>
#include <stdint.h>
#include "fix15.h"

#define SENSOR_MAX_UNITS   3          // unit ids 1..2 are used; index 0 unused
#define SENSOR_TIMEOUT_US  200000     // no data for this long = sensor lost

typedef struct {
    bool     valid;       // false until the first packet, or after a timeout
    fix15    angle;       // degrees
    fix15    rate;        // degrees/second
    uint32_t seq;         // sequence number of this sample
    uint32_t rx_us;       // server time_us_32() when it arrived
    uint16_t flags;       // SKI_FLAG_*
} sensor_sample_t;

// Per-unit receive statistics, mainly for the serial log and the report.
typedef struct {
    uint32_t received;    // accepted packets
    uint32_t gaps;        // packets missing between accepted sequence numbers
    uint32_t stale;       // dropped because seq was not newer
    uint32_t resets;      // seq tracking restarted (timeout or client reboot)
} sensor_stats_t;

// Start receiving. Call once after the network is up.
// Returns 0 on success.
int sensor_source_init(void);

// Copy the latest sample for unit_id. Returns false if the unit has never
// sent data or has timed out (out->valid is false then too).
bool sensor_source_get(uint8_t unit_id, sensor_sample_t *out);

// Copy the running statistics for unit_id.
void sensor_source_stats(uint8_t unit_id, sensor_stats_t *out);

// Packets rejected before reaching any unit (bad length, magic, version, id).
uint32_t sensor_source_invalid_count(void);

// Accepted samples queued for logging (every SKI_LOG_ANGLE_EVERY-th packet).
// The receive path cannot print, so the main loop drains this queue.
typedef struct {
    uint8_t  unit_id;
    uint32_t seq;
    fix15    angle;
    fix15    rate;
    uint16_t flags;
} sensor_log_entry_t;

// Pop the oldest queued entry. Returns false when the queue is empty.
bool sensor_source_next_log(sensor_log_entry_t *out);

#endif // SKISIM_SENSOR_SOURCE_H
