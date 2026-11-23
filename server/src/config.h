// Server settings: logging, DAC pins, steering, and game tuning.
// VGA pins are fixed by the course driver (GP16-GP21); the encoder joins later.
#ifndef SKISIM_SERVER_CONFIG_H
#define SKISIM_SERVER_CONFIG_H

// Print an "rx unit=.. seq=.. angle=.." line for every Nth accepted packet,
// for tools/imu_viewer and for comparing with the board's "tx" lines.
// 2 = 50 lines/s per unit. 0 = off.
#define SKI_LOG_ANGLE_EVERY 2

// ---------------------------------------------------------------------------
// Audio: MCP4822 DAC on SPI1, matching the wiring on the server breadboard:
//   GP10 (physical pin 14, yellow) -> MCP4822 pin 3 SCK
//   GP11 (physical pin 15, green)  -> MCP4822 pin 4 SDI
//   GP13 (physical pin 17, white)  -> MCP4822 pin 2 CS
//   3V3 -> pin 1 VDD; GND -> pin 5 LDAC and pin 7 VSS; pin 8 VOUTA -> audio jack.
// (Course Lab 1 used SPI0 on GP5/GP6/GP7; either works, the pins just have to match.)
// ---------------------------------------------------------------------------
#define SKI_DAC_SPI        spi1
#define SKI_DAC_PIN_CS     13     // physical pin 17
#define SKI_DAC_PIN_SCK    10     // physical pin 14
#define SKI_DAC_PIN_MOSI   11     // physical pin 15 -> MCP4822 SDI
#define SKI_DAC_SPI_HZ     20000000
#define SKI_AUDIO_RATE_HZ  25000  // DDS sample rate (timer ISR on core 1)
#define SKI_AUDIO_VOLUME   80     // master volume, 0..100

// ---------------------------------------------------------------------------
// Steering: board tilt -> steer in -1..+1
// ---------------------------------------------------------------------------
#define SKI_STEER_FULL_DEG   10.0f   // tilt that gives full steer (spec: about +-10 deg)
#define SKI_STEER_DEADZONE   0.5f    // degrees around level that count as straight
#define SKI_STEER_INVERT     0       // 1 if leaning right steers left on your board
#define SKI_ZERO_TRACK_ALPHA 0.02f   // how fast "level" follows the board in the menu

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------
#define SKI_GAME_TICK_MS     16      // game physics at about 60 Hz (core 0)
#define SKI_RUN_GATES        20      // gates per run
#define SKI_RESULT_SECONDS   6       // results screen time before the menu returns

#endif // SKISIM_SERVER_CONFIG_H
