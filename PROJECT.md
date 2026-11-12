# Ski Simulator: Project Specification (for the development agent)

Cornell ECE 5730 final project. The player stands on a board that rocks left and right. An IMU under the board measures its tilt angle, and a Raspberry Pi Pico W uses that angle to control a ski game on a VGA monitor while producing sound effects in real time.

This document is the single source of truth for development. The code is split into two folders:

- `server/`: **the host**. Drives the VGA monitor, speaker, and rotary encoder, and runs the game.
- `client/`: **the board unit**. Reads the MPU6050, computes the tilt angle, and sends it to the host over WiFi.

---

## 1. What We Are Building

| Item | Description |
| --- | --- |
| Input | Left/right tilt angle of the rocking board (single axis, about ±10°) |
| Output | 640×480 VGA display (slope seen from behind the skier, gates, skier, tilt indicator) and sound effects |
| Gameplay | The skier turns toward whichever side the board tilts; passing a gate scores points, missing one costs points; a results screen appears at the end of each run |
| Controls | Rotary encoder with push button: select mode, difficulty, sensitivity; start and restart |
| Measurements (for the report) | Angle accuracy, static noise and drift, WiFi latency distribution |

**Later, not now**: a chest unit (a second IMU measuring upper-body angle, used to tell carving from banking and give a technique score) and a load-cell force plate. The architecture must let the chest unit join later as a "second client" without changing the server's core logic.

---

## 2. Two Development Phases

### Phase 1: Two Pico W boards connected over WiFi (get the data path working first)

The client reads the MPU6050, computes the tilt angle, and sends it to the server over UDP. The server runs an access point, receives packets, and initially just prints the angle over the USB serial port. **No VGA, DAC, or encoder yet.** Both boards sit on the desk powered by USB.

**Done when**:
- Tilting the MPU6050 on the client by hand makes the angle on the server's serial output follow in real time, at about 100 samples per second.
- The latency distribution has been measured (mean, maximum, fraction above 20 ms).
- Unplugging the client's power and plugging it back in reconnects automatically, and the server does not crash.

### Phase 2: The game (add display, sound, and input to the server)

Connect the VGA, DAC, speaker, and encoder to the server, and implement calibration, the game, and sound. The client switches to battery power and is mounted under the rocking board.

**Done when**: with the client on battery only, a person standing on the board steers the skier, can finish a run, and sees the score.

### Fallback: Wired

If WiFi latency or reliability is not good enough, wire the MPU6050 directly to the server over I2C on GP8/GP9 and switch the server to the "local sensor source". The game code does not change.

```
Phase 1 (desk, USB power)                   Phase 2
┌────────────────────┐  WiFi UDP  ┌──────────┐     ┌────────────────────┐  WiFi UDP  ┌──────────┐
│ client (Pico W)    │───────────▶│ server   │     │ client (Pico W)    │───────────▶│ server   │
│ MPU6050            │            │ Pico W   │     │ MPU6050 + 3×AAA    │            │ Pico W   │
└────────────────────┘            └────┬─────┘     │ under the board    │            └────┬─────┘
                                       │           └────────────────────┘                 │
                                 USB serial                                     VGA monitor · speaker · encoder
```

---

## 3. Hardware

### 3.1 Parts

| Part | Used in | Notes |
| --- | --- | --- |
| Raspberry Pi Pico W (RP2040 + CYW43439) | one each for server and client | One board on hand; **the second one is needed from the start of Phase 1**. Supports WiFi and Bluetooth; this project only uses WiFi |
| MPU6050 (GY-521 module) | client | I2C address 0x68 (AD0 to GND). Moves to the server in the wired fallback |
| MCP4822 dual-channel 12-bit SPI DAC | server (Phase 2) | Channel A only |
| Speaker or audio jack | server (Phase 2) | Must be a powered speaker; the DAC cannot drive a bare speaker |
| VGA connector + resistors (470 Ω ×1, 330 Ω ×3) | server (Phase 2) | 4-bit color: 2 bits green, 1 bit blue, 1 bit red |
| KY-040 rotary encoder (with button) | server (Phase 2) | `+` to 3.3 V |
| 3×AAA battery holder (with switch), 1N5817 Schottky diode | client (Phase 2) | Battery feeds VSYS through the diode; Phase 1 uses USB power |
| Rocking balance board, yoga blocks | mechanical | Single-axis left/right rocking; yoga blocks provide centering |

### 3.2 Server Pins

The Phase 1 server has no external parts, only USB. The table below is the Phase 2 wiring. VGA and DAC pins follow the defaults of the course example code. **Before starting, compare against the pin comments at the top of the course Lab code; if they differ, the course code wins, and update this table.**

| GPIO | Physical pin | Function | Connects to |
| --- | --- | --- | --- |
| GP5 | 7 | SPI0 CS | MCP4822 pin 2 |
| GP6 | 9 | SPI0 SCK | MCP4822 pin 3 |
| GP7 | 10 | SPI0 TX | MCP4822 pin 4 SDI |
| GP8 | 11 | I2C0 SDA | Reserved: MPU6050 SDA in the wired fallback |
| GP9 | 12 | I2C0 SCL | Reserved: MPU6050 SCL in the wired fallback |
| GP10 | 14 | Encoder A | KY-040 CLK |
| GP11 | 15 | Encoder B | KY-040 DT |
| GP12 | 16 | Encoder button | KY-040 SW (enable internal pull-up) |
| GP16 | 21 | VGA HSYNC | DB15 pin 13 |
| GP17 | 22 | VGA VSYNC | DB15 pin 14 |
| GP18 | 24 | VGA green low bit | 470 Ω → DB15 pin 2 |
| GP19 | 25 | VGA green high bit | 330 Ω → DB15 pin 2 |
| GP20 | 26 | VGA blue | 330 Ω → DB15 pin 3 |
| GP21 | 27 | VGA red | 330 Ω → DB15 pin 1 |

MCP4822 LDAC (pin 5) goes to GND; VOUTA (pin 8) goes to the speaker.

### 3.3 Client Pins

| GPIO | Physical pin | Function | Connects to |
| --- | --- | --- | --- |
| GP8 | 11 | I2C0 SDA | MPU6050 SDA |
| GP9 | 12 | I2C0 SCL | MPU6050 SCL |
| 3V3(OUT) | 36 | 3.3 V | MPU6050 VCC |
| VSYS | 39 | Power input | 1N5817 cathode (anode to battery +) |
| GND | 38 | Ground | Battery −, MPU6050 GND |

GP2–GP6 are reserved for future HX711 load cells (GP2 shared SCK, GP3–GP6 one DOUT each). Do not use them now.

### 3.4 Resources That Must Not Be Used

- On the Pico W, GP23, GP24, GP25, and GP29 are wired internally to the wireless chip and must not be used. The on-board LED is controlled by the wireless chip: use `cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ...)`.
- The wireless driver takes one PIO state machine and one DMA channel. VGA goes on `pio0` (the course driver hard-codes `pio0` and uses three state machines), and the wireless driver uses `pio1`. Initialize VGA before the wireless driver so it cannot claim `pio0` first, and verify this on hardware.

The full breadboard wiring diagram (with numbered wires and positions) is in a separate HTML wiring page. The pin tables in this document match it.

---

## 4. Software Environment

- C, Raspberry Pi Pico SDK, CMake.
- `PICO_BOARD=pico_w` for both server and client; otherwise the wireless chip and LED do not work. (Older course Lab projects use `pico2`; do not copy that setting.)
- Use course-provided code as is; do not rewrite it:
  - VGA driver (the `vga16_graphics` files and their `.pio` files), used for drawing on core 1.
  - Protothreads header (the course version, `pt_cornell_rp2040_*.h`).
  - Copy these files from the course website or Lab code into `server/lib/`. **Do not rewrite their APIs from memory**; when unsure, the actual file contents are authoritative.
  - The one allowed edit: switch the VGA driver to single buffering (see Section 9).
- Networking: lwIP, linking `pico_cyw43_arch_lwip_threadsafe_background`.
- Numerics: use the course's `fix15` fixed-point format for filtering and game physics (the RP2040 has no hardware FPU). The `atan2` that converts accelerometer readings to an angle runs once per sample and may use float.

---

## 5. How It Works

### 5.1 Client (board unit)

1. Startup: initialize I2C0 (100 kHz), wake the MPU6050, configure ±2 g, ±250 °/s, and the built-in low-pass filter.
2. Hold still for 2 seconds and average the angular rate to get the gyro bias.
3. Read the MPU6050 from a **500 Hz** repeating timer and run a complementary filter:

   `angle = α · (angle + (gyro − bias) · dt) + (1 − α) · accel_angle`, where `accel_angle = atan2(a_y, a_z)`, `α ≈ 0.98`, `dt = 2 ms`

   Adjust the axes to match how the MPU6050 is actually mounted. Put this in a single config constant; do not scatter it through the code.
4. The main loop sends one UDP packet every **10 ms** (format in Section 6) and answers the server's pings.
5. Reconnect automatically when the connection drops; keep running the filter while disconnected, without interrupting sampling.

### 5.2 Server (host)

| Core | Work |
| --- | --- |
| core 0 | Get the latest angle (local or UDP), calibration and normalization, game logic at 60 Hz, encoder, sound parameters |
| core 1 | VGA drawing (course driver); each frame takes a snapshot of the game state from core 0 and draws it |

- **Sensor source abstraction**: the game calls a single interface to get "latest angle, angular rate, timestamp, valid flag".
  - Default: `source_udp.c`, receives UDP packets (used from Phase 1 on).
  - Wired fallback: `source_local.c`, calls the IMU and filter modules in `client/shared` directly.
  - Select with the CMake option `SKI_SOURCE=UDP|LOCAL`; default is `UDP`.
- **The Phase 1 server** only handles connectivity and data: run the AP, receive packets, print angle and sequence number over serial, run latency measurements. Game, calibration, sound, and encoder below are Phase 2 work.
- **Calibration**: before starting, ask the player to stand level, lean fully left, and lean fully right, holding each for 1 second. Record the three angles, then normalize the angle to a `steer` value from −1 to +1 with a small dead zone.
- **Game**: the slope scrolls into the screen; `steer` sets the skier's lateral speed; gates appear at fixed intervals. Passing and missing a gate are each detected. Speed, gates passed, and score are shown on screen.
- **Sound**: the MCP4822 is driven by a timer ISR (direct digital synthesis). The carving hiss volume follows `|steer|`, passing a gate plays a chime, and missing a gate plays a crash sound. All sounds are synthesized; no recordings.
- **Encoder**: select mode, difficulty, sensitivity; button starts or restarts.
- **States**: `CALIBRATE → MENU → RUN → RESULT`. If the sensor source has no new data for more than 200 ms, the screen shows "Sensor lost" and the game pauses.

---

## 6. Communication Protocol (used from Phase 1 on)

| Item | Setting |
| --- | --- |
| Topology | The server runs a WiFi AP and the client connects to it. No campus network |
| SSID / password | `skisim` / `skisim-5730` (defined in one config header) |
| IP | Server `192.168.4.1`; client uses static IP `192.168.4.2` (no DHCP server needed) |
| Ports | Data `4210` (client → server); ping `4211` (both directions) |
| Power saving | Disabled on both ends (`cyw43_wifi_pm` set to no power saving); this is the largest source of latency |

Data packet, little-endian, `__attribute__((packed))`, defined in the shared header `protocol.h`:

| Field | Type | Description |
| --- | --- | --- |
| `magic` | `uint16_t` | Always `0x5A4B` |
| `version` | `uint8_t` | Protocol version, currently `1` |
| `unit_id` | `uint8_t` | `1` = board unit, `2` = chest unit (later) |
| `seq` | `uint32_t` | Incremented by 1 on every send |
| `t_us` | `uint32_t` | Client `time_us_32()` at send time |
| `angle` | `int32_t` | Tilt angle, fix15, in degrees |
| `rate` | `int32_t` | Angular rate, fix15, in degrees/second |
| `flags` | `uint16_t` | bit0 = gyro calibration done, bit1 = IMU read error |

Server handling rules:

- Drop the packet if `magic` or `version` does not match.
- Track each `unit_id` separately; drop a packet whose `seq` is not greater than the previous one.
- Reset a unit's `seq` tracking when it has timed out (no packet for more than 200 ms) or when `seq` jumps far backwards. A rebooted client restarts `seq` from 0, and without this reset the server would drop all of its packets.
- When no new packet arrives, keep using the last one. No retransmission, no queueing.
- Latency measurement: the server sends `{magic, seq, t_us}` to the client's port 4211, the client echoes it back unchanged, and the server estimates one-way latency as half the round-trip time. Collect a few thousand samples and print the mean, maximum, and fraction above 20 ms over USB serial.

---

## 7. Folder Structure

```
SkiSim/
├── PROJECT.md                 this document
├── client/                    board unit (the Phase 2 Pico W)
│   ├── CMakeLists.txt
│   ├── pico_sdk_import.cmake
│   ├── shared/                code the server also uses; the only copy
│   │   ├── protocol.h         packet format, ports, SSID, etc.
│   │   ├── fix15.h            fixed-point macros
│   │   ├── mpu6050.c / .h     I2C read/write, initialization
│   │   └── tilt_filter.c / .h gyro bias, complementary filter
│   └── src/
│       ├── main.c             init, 500 Hz sampling, send every 10 ms
│       ├── net_client.c / .h  join AP, send UDP, answer pings, reconnect
│       └── config.h           axes, α, pins
└── server/                    host
    ├── CMakeLists.txt         selects the sensor source via SKI_SOURCE; references ../client/shared
    ├── pico_sdk_import.cmake
    ├── lib/                   course VGA driver, .pio files, protothreads (copied as is)
    └── src/
        ├── main.c             start both cores, state machine
        ├── sensor_source.h    sensor source interface
        ├── source_udp.c       default: UDP receive, latency measurement
        ├── source_local.c     wired fallback: local MPU6050
        ├── net_ap.c / .h      run AP, static IP
        ├── calibrate.c / .h   three-point calibration, steer normalization
        ├── game.c / .h        slope, gates, collision, scoring (core 0)
        ├── render.c / .h      VGA drawing (core 1)
        ├── audio.c / .h       DAC timer ISR, sound synthesis
        └── input.c / .h       encoder and button
```

Rules:

- Code shared by both ends lives only in `client/shared/`. The server's CMake references it by relative path; do not make a copy.
- Pins, SSID, ports, and filter parameters all live in config headers; do not hard-code them in functions.

---

## 8. Development Order and Acceptance

| Step | Work | Acceptance |
| --- | --- | --- |
| 1 | Server and client skeletons: blink, USB serial output | Both LEDs blink; serial output appears |
| 2 | Server runs AP (static IP 192.168.4.1); client connects (192.168.4.2) | Both serial outputs report a successful connection; a laptop can see the `skisim` network |
| 3 | Client sends test packets (fixed values); server receives and prints them | Server prints increasing `seq` with no or very few gaps |
| 4 | MPU6050 driver and filter in `client/shared`, running on the client | I2C scan finds 0x68; angle is stable at rest and follows manual tilting |
| 5 | Client sends the real angle every 10 ms; server receives, drops stale packets, detects timeouts | Tilting the MPU6050 makes the angle on the server's serial output follow in real time |
| 6 | Latency measurement (ping port 4211) | Latency distribution printed; measured once before and once after disabling power saving for comparison |
| 7 | Reconnection | Unplug and replug client power; it recovers automatically and the server does not crash |
| **Phase 1 complete** | | |
| 8 | Server drives VGA: draw slope, gates, skier, controlled by received angle; **check RAM first** | Tilting the client turns the skier in real time; `arm-none-eabi-size` shows RAM headroom |
| 9 | Calibration and `steer` normalization | After calibration, people of different weights all reach ±1 at maximum tilt |
| 10 | Game rules, scoring, state machine | Can play a full run from the menu and see the results |
| 11 | Sound and encoder menu | Carving sound changes with tilt; gate pass and miss sounds are correct |
| 12 | Client on battery, mounted under the rocking board | Fully playable with the client's USB unplugged |
| **Phase 2 complete** | | |

---

## 9. Known Limitations and Notes

- **Server RAM will be tight**: the RP2040 has only 264 KB. One 16-color 640×480 frame buffer is 153.6 KB. The course VGA driver (`vga16_graphics_v3.c`) defaults to double buffering (`DOUBLE_BUFFER_60`, about 307 KB), which does not fit on the RP2040 at all. **Change it to `DOUBLE_BUFFER_NONE`**; this is the only edit allowed to course files. That leaves about 110 KB for lwIP, both cores' stacks, and game state. The cost is possible tearing or flicker, since drawing happens on the displayed buffer; reduce it by redrawing only regions that changed. Phase 1 has no VGA so this is not an issue; the risk appears at Phase 2 Step 8 when VGA is added. Check with `arm-none-eabi-size` after every build. If RAM still runs out or flicker is unacceptable, the fallback is to switch the server to a Pico 2 W (RP2350) with the same pins and architecture. Do not lower the resolution to save RAM without first confirming with the user.
- **On the client, the top + rail is battery positive (about 4.5 V)**. The MPU6050 VCC must go to the Pico's 3V3 (pin 36).
- Turn off the battery holder switch before plugging in USB to flash.
- Before changing the system clock, confirm WiFi still works; the wireless chip's interface clock is divided down from the system clock.
- Keep the I2C wires running under the board within 50 cm and use 100 kHz.

---

## 10. Working Rules for the Development Agent

- Follow the order in Section 8. Each step must be independently verifiable; do not write the whole system at once.
- When changing pins, the protocol, or the folder structure, update this document at the same time.
- Use course-provided code (VGA driver, protothreads) as is. If you do not know its API, ask the user for the file first; do not guess.
- For parts that cannot be verified without hardware (I2C, VGA timing, WiFi), write down clearly how the user should test on real hardware and what they should expect to see.
- Keep the chest unit addable later: the server dispatches by `unit_id`, and game logic must not assume a single sensor source.
- All repository content (docs, code comments, commit messages) is written in English.

## 11. Open Questions

- DAC pins: the VGA (GP16–GP21) and MPU6050 I2C (GP8/GP9) pins in Section 3.2 have been confirmed against the course `MPU6050_IMU_Demo`. The DAC pins (GP5–GP7) still need to be checked against a course Lab that uses the DAC.
- The Lab 3 MPU6050 driver (`mpu6050.c/.h`) can move into `client/shared/`, but its I2C pin defines live in `mpu6050.h` and must move to a config header.
- Whether WiFi is allowed for the final project (confirm with the TA).
- When the second Pico W arrives: Phase 1 needs both boards from the start.
