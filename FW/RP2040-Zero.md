# Waveshare RP2040-Zero build (1 port)

This is a build of the adapter firmware for a Waveshare RP2040-Zero with a single GameCube controller port. It is enabled with the `BOARD_RP2040_ZERO` CMake option.

## Build

```sh
cd FW
cmake -B build-zero -DBOARD_RP2040_ZERO=ON
cmake --build build-zero -j
# output: FW/build-zero/gc_adapter_rp2040.uf2
```

Use a separate build directory from the normal GCP+ build, because `PICO_BOARD` is cached.

## Parts

| Part | Required | Notes |
|---|---|---|
| Waveshare RP2040-Zero | yes | |
| GameCube controller socket | yes | Use a replacement console port, or cut a GC extension cable and use its female end |
| 1 kΩ resistor | yes | Pull-up from Data to 3V3. The line is open-drain, and the firmware's first-boot test fails without it (see below) |
| Momentary push button | no | For switching modes. The RP2040-Zero's BOOT button is on the flash chip select, not a GPIO, so it can't be used |
| 10–47 µF capacitor | no | Across 3V3 and GND, close to the socket |
| 500 mA polyfuse | no | In series with the 5 V rumble line |

## Wiring

GameCube socket pins, using the standard numbering:

| GC pin | Signal | RP2040-Zero |
|---|---|---|
| 1 | 5 V (rumble motor) | `5V` |
| 2 | Data | `GP0`, plus 1 kΩ from `GP0` to `3V3` |
| 3 | GND | `GND` |
| 4 | GND | `GND` |
| 5 | not connected | |
| 6 | 3.3 V (logic) | `3V3` |
| 7 | Cable shield | `GND` |

Mode button (optional): connect it between `GP14` and `GND`. The firmware enables the internal pull-up.

The status LED is the onboard WS2812 on `GP16`.

Wire colours vary between extension cables, so check each wire with a multimeter before soldering.

To use different pins, change `JOYBUS_PORT_1` in `include/main.h` and `ADAPTER_BUTTON_1` in `include/adapter_config.h`.

## Behaviour

- **First boot:** the firmware checks that the data line reads high. If it doesn't (usually because the 1 kΩ pull-up is missing), the LED turns orange and the firmware stops. The check passes once and is then remembered.
- **Mode colours:** the LED shows the mode at boot:
  - yellow: Switch Pro
  - green: XInput
  - purple: GC adapter
  - cyan: Slippi
- **Controller status:** after boot, the LED turns white when a controller is connected and red when it is removed.
- **Button:** a short press moves to the next mode (the device reboots). Holding for 5 s saves the current mode as the default. Mode changes are ignored while a controller is connected.
- **Bootloader:** hold the button, or the BOOT button, while plugging in USB.
- **LED colours wrong:** if colours show with red and green swapped (for example, yellow looks wrong), set `UTIL_RGB_SWAP_RG` to `1` in `include/main.h`.
- **GC adapter and Slippi modes:** these always report four ports over USB, and the controller appears as port 1. Switch Pro and XInput modes show one controller.
