# VGA link test (Phase 2, step 8a)

Goal: the server Pico W drives the VGA monitor and shows the tilt angle that the board Pico W sends over WiFi.

## 1. Wire the VGA to the server Pico W

Use the lab VGA connector board if you have it; the pins are the same as in the labs. Unplug USB while wiring.

| Server Pico W | Physical pin | Through | VGA (DB15) pin |
| --- | --- | --- | --- |
| GP16 | 21 | wire | 13 (HSYNC) |
| GP17 | 22 | wire | 14 (VSYNC) |
| GP18 | 24 | 470 Ω | 2 (green) |
| GP19 | 25 | 330 Ω | 2 (green) |
| GP20 | 26 | 330 Ω | 3 (blue) |
| GP21 | 27 | 330 Ω | 1 (red) |
| GND | 23 or 28 | wire | 5, 6, 7, 8, 10 (ground) |

Check before powering on:

- Both green resistors (470 Ω and 330 Ω) meet at DB15 pin 2.
- Pico GND is connected to the VGA ground pins.
- Nothing is on GP23, GP24, GP25 or GP29 (used by the wireless chip).

## 2. Build and flash

```bash
cd server
cmake -S . -B build -G Ninja && ninja -C build
```

Hold BOOTSEL, plug in USB, and copy `build/skisim_server.uf2` to the drive. The board unit keeps its current firmware.

## 3. What you should see

Open the server's serial port at 115200 baud.

| Step | Serial output | VGA monitor |
| --- | --- | --- |
| Server powered, board unit off | `sys clock 150000 kHz`, `VGA started on core 1`, then the AP starting | Title "SkiSim link test", yellow **WAITING FOR BOARD**, heartbeat square blinking top right, `VGA frame` counting up |
| Board unit powered on | `stations: 0 -> 1`, then `unit 1: ~100 pkt/s` | Green **LIVE**, `WiFi clients 1`, `packets/s` near 100, `seq` increasing |
| Tilt the IMU by hand | angle follows | Big angle number, yellow needle and cyan board line all follow the tilt |
| Unplug the board unit | `SENSOR LOST` | Red **SENSOR LOST** within 0.2 s, needle turns red |
| Plug it back in | packets resume, `resets` increases | Back to **LIVE** without resetting the server |

## 4. If something is wrong

| Symptom | Likely cause | Check |
| --- | --- | --- |
| Monitor says "no signal" | HSYNC/VSYNC wiring, or wrong clock | GP16 to pin 13, GP17 to pin 14; serial must say `sys clock 150000 kHz` |
| Picture rolls, is shifted, or the monitor reports an odd mode | Clock not at 150 MHz | Same as above; rebuild from a clean `build` folder |
| Picture is there but colors are wrong | Color resistor wiring | Green on pin 2, blue on pin 3, red on pin 1 |
| Screen is fine but stays on WAITING FOR BOARD | Board unit not connected | Board serial should report joining `skisim`; server serial `stations` should be 1 |
| `stations` stays 0 and the AP never shows on a laptop | WiFi not working at 150 MHz | Add `CYW43_PIO_CLOCK_DIV_INT=3` to `target_compile_definitions` in `server/CMakeLists.txt`, rebuild, retest; report the result |
| Angle moves the wrong way on screen | IMU axis or sign | Fix the axis/sign constant on the board unit, not in the server |
| Some flicker on the changing numbers | Single buffer (expected, small) | Report it if it is distracting; the game screen will redraw only changed regions |
