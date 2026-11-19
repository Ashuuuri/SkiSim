// Server settings. Pins for VGA, DAC, and the encoder will join this file
// in Phase 2.
#ifndef SKISIM_SERVER_CONFIG_H
#define SKISIM_SERVER_CONFIG_H

// Print an "rx unit=.. seq=.. angle=.." line for every Nth accepted packet,
// for tools/imu_viewer and for comparing with the board's "tx" lines.
// 2 = 50 lines/s per unit. 0 = off.
#define SKI_LOG_ANGLE_EVERY 2

#endif // SKISIM_SERVER_CONFIG_H
