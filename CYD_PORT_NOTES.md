# CYD port - ESP32-2432S028R

This version is a hardware port of ESP32-Plane-Radar for the ESP32-2432S028R (CYD).

## Target hardware

- ESP32-WROOM-32 / `esp32dev`
- ILI9341 240x320 display, used in 320x240 landscape mode
- Display SPI: SCLK 14, MOSI 13, MISO 12, CS 15, DC 2
- Display reset: connected to board reset (`-1` in LovyanGFX)
- Backlight: GPIO21, active HIGH
- BOOT button: GPIO0
- XPT2046 touch: SCLK 25, MOSI 32, MISO 39, CS 33

## UI

The existing radar renderer remains a 240x240 square and is aligned to the left. The 80-pixel control strip on the right shows the aircraft count and touch zoom buttons. Touching the radar area toggles the persistent animated sweep. The web configuration has a persistent **Disable radar sweep** checkbox that forces the sweep off and blocks the radar touch action.

ADS-B HTTPS fetching is pinned to ESP32 core 0. The Arduino display, touch, portal, and sweep loop remains on core 1; aircraft data is double-buffered before it crosses between the tasks.

## LovyanGFX

The ILI9341 panel is configured with `offset_rotation = 2` and the application uses `setRotation(1)` to obtain landscape orientation.

If the display has correct geometry but red/blue are swapped, change `kDisplayRgbOrder` in `include/config.h` from `false` to `true`.

## Build

Use PlatformIO:

    pio run -e cyd

Upload:

    pio run -e cyd -t upload

The project retains the existing custom partition table and firmware merge script.
