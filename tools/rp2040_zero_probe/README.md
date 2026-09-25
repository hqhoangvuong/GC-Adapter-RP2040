# RP2040-Zero probe

A diagnostic firmware for running the GC adapter firmware on a Waveshare
RP2040-Zero or a clone. It reports the board details the adapter depends on.

## Use it

1. Hold **BOOT**, plug the board in, and release. An `RPI-RP2` drive appears.
2. Copy `rp2040_zero_probe.uf2` onto it. The board reboots and the onboard LED
   breathes dim white while it waits for you.
3. Open the board's USB serial port with any terminal (any baud rate): the
   Arduino IDE Serial Monitor, PuTTY, `screen`, or a Web Serial page in Chrome.
4. The report prints when the port opens. Copy everything from
   `=== Summary` to the end, since that block is the part to share.

Commands, typed into the terminal:

| Key | Action |
|---|---|
| `r` | Rerun the report |
| `j` | Controller test on every pulled-up pin: ID, rest position, 3 s of live input, 1 s of rumble |
| `l` | LED colour-order test (watch the LED and note 3 colours) |
| `m` | Live pin monitor: press your buttons or plug in controllers to see which GPIO changes |
| `b` | Reboot into BOOTSEL to flash something else |

## What it checks

- **Flash**: JEDEC vendor and size, and the *real* size, found by where the
  address space wraps (clones sometimes relabel chips). It also checks whether
  the adapter's settings location (1204 KB) fits on the chip.
- **Clocks**: crystal frequency (the firmware expects 12 MHz) and chip revision.
- **Every GPIO except GP16**: floating, external pull-up, or held low. It uses
  only the chip's internal pulls to decide.
- **Pull-up strength**: on pins with an external pull-up, it pulls the line
  low for 5 µs and times the rise. GC data lines need a strong pull-up (about
  1 kΩ to 3V3), so the rise should be well under 300 ns.
- **GameCube controllers**: sends the probe command on each pulled-up pin and
  decodes the reply.

## Safety

The probe only enables internal pull-ups and pull-downs, except on pins that
already read high against the pull-down. It pulls those low for 5 µs, which is
harmless for a pull-up resistor or a controller data line. The `j` test drives
those same pins with the GameCube protocol.

## Build

The `.uf2` in this folder is prebuilt. To rebuild with Pico SDK 1.5.1:

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S tools/rp2040_zero_probe -B build-probe -G Ninja
ninja -C build-probe
```

It uses the generic flash boot stage (`boot2_generic_03h`) so it boots on any
flash chip, and it reuses `FW/pio/ws2812.pio` for the LED.
