# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository overview

Firmware, PCB and enclosure for the Hand Held Legend **GC Pocket Adapter+**: an RP2040 board that reads up to four GameCube controllers over Joybus and presents them over USB as a Switch Pro controller, XInput, GameCube adapter (OEM/"GC mode") or Slippi (1 kHz) device.

- `FW/`: RP2040 firmware (Pico SDK + TinyUSB, CMake). This is the only code.
- `PCB/`: KiCad project (`GCP-2.*`) plus a copy of a prebuilt `.uf2`.
- `Production/`: gerbers, BOM, placement files, and 3D STEP/Plasticity models for the shell.

Licensing: firmware and PCB are CC BY-NC-SA 3.0; the 3D models are CC BY 4.0.

## Building the firmware

Most of the firmware lives in the git submodule `FW/common` ([HHL-GC-Common](https://github.com/mitchellcairns/HHL-GC-Common)). The build fails without it:

```sh
git submodule update --init
```

The build expects Pico SDK 1.5.1 and ARM GCC 13.2. It picks up the SDK from the Raspberry Pi Pico VS Code extension (`~/.pico-sdk`) if that is installed. Otherwise set `PICO_SDK_PATH`, or set `PICO_SDK_FETCH_FROM_GIT=1`.

```sh
cd FW
cmake -B build -DPICO_BOARD=pico
cmake --build build -j
# output: FW/build/gc_adapter_rp2040.uf2 (flash by dragging onto the RP2040 BOOTSEL drive)
```

There are no tests, linters or CI. `FW/oldCMakeLists.txt` is an older version of the build file and is not used.

Build side effects:
- `pico_generate_pio_header` regenerates `FW/include/generated/{joybus,ws2812}.pio.h` from `FW/pio/*.pio`. These generated headers are committed, so check them in again after editing a `.pio` file.
- A post-build step (`FW/manifest.cmake`) reads `ADAPTER_FIRMWARE_VERSION` from `FW/include/adapter_config.h` and writes it as a decimal number to `fw_version` in `FW/manifest.json`. To release, bump the hex version in `adapter_config.h`, update the `changelog` in `manifest.json`, and rebuild.
- `.gitignore` ignores `FW/build/*` except for `gc_adapter_rp2040.uf2`, so the release binary is committed.

### Waveshare RP2040-Zero variant

`-DBOARD_RP2040_ZERO=ON` builds a 1-port version for the Waveshare RP2040-Zero. It sets `PICO_BOARD=waveshare_rp2040_zero` and defines `BOARD_RP2040_ZERO`, which switches the settings in `adapter_config.h` and `main.h`:
- data pin GP0
- one button (GP14)
- one LED (GP16)
- `ADAPTER_PORT_COUNT 1`

Build it in its own directory (e.g. `build-zero`), because `PICO_BOARD` is cached. Wiring and behaviour are described in `FW/RP2040-Zero.md`.

Code that loops over ports must use `ADAPTER_PORT_COUNT`, not 4. Keep the per-port arrays (`_port_joybus[4]` etc.) at size 4, because `common` (`devices/gcinput.c`, `ll/adapter_ll_rp2040.c`) always reads four entries.

## Firmware architecture

The shared, board-independent adapter logic is in `FW/common`. The same library also targets ESP32 (`ADAPTER_MCU_TYPE`: 1 = RP2040, 2 = ESP32). This repo supplies only the board-specific pieces, and `common` calls into them:

| This repo provides | Called from `common` |
|---|---|
| `joybus_itf_init/poll/enable_rumble` (`FW/src/joybus_itf.c`) | `utilities/joybus.c`, `adapter.c` main loop, `adapter_tusb.c` / `switch_commands.c` for rumble |
| `rgb_itf_init/update` (`FW/src/rgb_itf.c`) | `utilities/rgb.c` |
| `cb_adapter_hardware_test` (`FW/src/main.c`) | `adapter_settings.c` / `adapter.c`, on first boot, before `adapter_hardware_test` is saved in settings |
| `FW/include/adapter_config.h`, `FW/include/main.h` | Board constants: pins, port/LED counts, USB strings, firmware and settings versions, WebUSB URL |

`main()` just calls `adapter_main_init()` then `adapter_main_loop()` (both in `common/adapter.c`). That code:
- loads settings from flash,
- chooses the input mode from reboot memory or saved settings, and shows it on the LEDs,
- starts TinyUSB with that mode's descriptors (`common/devices/*`, `common/usb/*`, `common/switch/*`),
- runs a single-core loop that polls Joybus once per USB SOF, sends a report at the configured rate (`USBRATE_1/4/8`), and calls `tud_task()`.

Changing the mode with the two buttons (`ADAPTER_BUTTON_1/2`) reboots the device, carrying the new mode in reboot memory. Mode changes are ignored while any controller is connected.

### Joybus (`FW/src/joybus_itf.c` + `FW/pio/joybus.pio`)
- `pio0` runs one state machine per port, SM index = port index. The data pins are consecutive, starting at `JOYBUS_PORT_1` (GPIO 22–25 on the GCP+). The WS2812 LEDs run on `pio1`, SM 0, GPIO 10 on the GCP+.
- Each port goes through three phases in `_port_phases[]`:
  - 0: probe with `0x00`
  - 1: origin request `0x41`. Its reply sets per-port analog offsets so sticks centre on 128 and triggers on 0. The port also gets the lowest free USB interface slot (`port_itf`).
  - 2: poll with `0x40 0x03 <rumble>`. The offsets are applied to each reply.
- After 10 consecutive empty reads a port resets to phase 0 and `port_itf = -1`.
- Every poll sends to all `ADAPTER_PORT_COUNT` SMs, waits 500 µs, then drains their RX FIFOs. Timing here matters, so keep the poll path non-blocking and short.
- `joybus_itf_enable_rumble` takes a **USB interface** index, not a physical port. It looks up the port through `port_itf`.
