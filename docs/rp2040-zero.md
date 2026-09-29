# Building a GC adapter on a Waveshare RP2040-Zero

This guide covers running the GC adapter firmware on a Waveshare RP2040-Zero
or a clone, wired by hand to GameCube controller sockets or cut extension
cables.

## Firmware

Prebuilt files are in `prebuilt/rp2040_zero/`:

| File | When to use |
|---|---|
| `gc_adapter_rp2040_zero.uf2` | Start here. Uses the fast flash mode, which suits Winbond and GigaDevice flash (genuine Waveshare boards). |
| `gc_adapter_rp2040_zero_generic_flash.uf2` | If the first one doesn't boot (LED never lights, no USB device). Works with any flash chip. |

The `tools/rp2040_zero_probe` report tells you your flash vendor.

To flash: hold **BOOT**, plug the board in, and copy the `.uf2` onto the
`RPI-RP2` drive.

To build it yourself:

```sh
git submodule update --init
cmake -S FW -B build-zero -G Ninja -DADAPTER_BOARD=rp2040_zero
ninja -C build-zero
```

Build options:

| Option | Default | Meaning |
|---|---|---|
| `ADAPTER_BOARD` | `gcp2` | `rp2040_zero` for this board |
| `ADAPTER_GC_PIN_BASE` | `26` | First GC data pin. Ports 1–4 use BASE..BASE+3, which must be consecutive. |
| `ADAPTER_GENERIC_FLASH_BOOT` | `OFF` | Use the generic flash boot stage |
| `ADAPTER_LED_RGB_ORDER` | `OFF` | Turn on if the LED shows red and green swapped |

## Pins used

| RP2040-Zero pin | Use |
|---|---|
| GP26 | Port 1 data |
| GP27 | Port 2 data |
| GP28 | Port 3 data |
| GP29 | Port 4 data |
| GP11 | Mode button "back" (to GND) |
| GP12 | Mode button "forward" (to GND) |
| GP16 | Onboard RGB LED (already on the board) |
| 3V3 | Controller 3.3 V and the data pull-ups |
| 5V | Controller rumble supply (this is the USB 5 V) |
| GND | Controller grounds, buttons |

On a genuine RP2040-Zero, GP26–GP29 sit on the same edge as 5V, GND and 3V3.
Check your clone's silkscreen, since some layouts differ.

## Wiring one controller port

```
                         RP2040-Zero
   GC port                ┌─────────┐
   5V (rumble)  ─────────►│ 5V      │
   GND          ─────────►│ GND     │
   GND (2nd)    ─────────►│ GND     │
   3.3V         ─────────►│ 3V3 ────┼──┐
                          │         │  │ 1 kΩ pull-up
   DATA         ─────────►│ GP26 ◄──┼──┘ (one per data pin)
                          └─────────┘
```

Repeat for ports 2–4 on GP27, GP28 and GP29. All ports share 5V, 3V3 and GND.

### Controller cable wires

On an original Nintendo controller cable (the usual, widely documented colours):

| Wire colour | Function | Goes to |
|---|---|---|
| Yellow | 5 V, rumble motor only | 5V |
| Red | Data | GP26–29, plus 1 kΩ to 3V3 |
| Green | Ground | GND |
| White | Ground | GND |
| Blue | 3.3 V, controller logic | 3V3 |
| Black (braid) | Cable shield | GND (optional) |

Third-party controllers and extension cables often use other colours, and pin
numbers differ between sources depending on which side of the plug is drawn.
**Identify wires by function, not by number or colour, unless you've confirmed
them.**

### Rules that protect your hardware

- **Never put 5 V on the data line or the 3.3 V line.** RP2040 pins are not
  5 V tolerant, and the controller logic runs at 3.3 V.
- **Fit a 1 kΩ pull-up on all four data pins, even if you only wire one port.**
  The firmware's self-test checks all four lines at first boot. Without the
  pull-ups it stops with an **orange** LED.
- The chip's internal pull-up is far too weak for the controller signal. The
  external 1 kΩ is required.

### Safe bring-up order

1. Fit the four 1 kΩ pull-ups. Flash `tools/rp2040_zero_probe` and check that
   GP26–GP29 show **EXTERNAL PULL-UP** with a fast rise (well under 300 ns).
2. Connect **only GND, 3.3 V and DATA** from one controller. Press `j` in the
   probe. A reply of `09 00 ..` and live stick values mean these three wires
   are right. A controller works without the 5 V wire; it just can't rumble.
3. Add the **5 V** wire and press `j` again. The controller should rumble
   for 1 second.
4. Flash `gc_adapter_rp2040_zero.uf2`.

## Buttons (optional but recommended)

Each button connects its pin to GND. Without buttons the adapter stays in its
default mode, Switch Pro Controller.

- GP11 press and release: previous mode. GP12 press and release: next mode.
- Both together, then release: save the current mode as the default.
- Mode changes only work while **no controller is plugged in**.
- Holding either button while plugging in USB enters BOOTSEL (firmware update).

## LED

The Zero has one LED. At power-up it shows the mode:

| Colour | Meaning |
|---|---|
| Yellow | Switch Pro Controller |
| Green | XInput (Xbox 360) |
| Purple | GameCube adapter (Dolphin / Wii U) |
| Cyan | Slippi (1 kHz) |
| Orange (stays) | Self-test failed: a data pull-up is missing |
| Red at boot | USB failed to start |

After that it shows port 1: white when a controller connects, red when it
disconnects.

If yellow looks orange-ish and green looks red, your LED uses RGB order. Build
with `-DADAPTER_LED_RGB_ORDER=ON`, or use the probe's `l` test to confirm.

## Power

Everything runs from the USB port. Rumble motors draw from the 5V pin, so
several controllers rumbling at once need a port and cable that can supply
it. The board's 3.3 V regulator also powers the controllers' logic, which
is fine for four controllers.
