# Building a GC adapter on a Waveshare RP2040-Zero

This guide covers running the GC adapter firmware on a Waveshare RP2040-Zero
or a clone, wired by hand to GameCube controller sockets or cut extension
cables.

## Firmware

Prebuilt files are in `prebuilt/rp2040_zero/`:

| File | Ports | Flash mode |
|---|---|---|
| `gc_adapter_rp2040_zero_1port.uf2` | 1 (GP26) | Fast. Suits Winbond and GigaDevice flash (genuine Waveshare boards). |
| `gc_adapter_rp2040_zero_1port_generic_flash.uf2` | 1 (GP26) | Generic. Works with any flash chip. |
| `gc_adapter_rp2040_zero_4port.uf2` | 4 (GP26–GP29) | Fast |
| `gc_adapter_rp2040_zero_4port_generic_flash.uf2` | 4 (GP26–GP29) | Generic |

Start with the fast version. If it doesn't boot (LED never lights, no USB
device), use the generic one.

The `tools/rp2040_zero_probe` report tells you your flash vendor.

To flash: hold **BOOT**, plug the board in, and copy the `.uf2` onto the
`RPI-RP2` drive.

To build it yourself:

```sh
git submodule update --init
cmake -S FW -B build-zero -G Ninja -DADAPTER_BOARD=rp2040_zero -DADAPTER_PORT_COUNT=1
ninja -C build-zero
```

Build options:

| Option | Default | Meaning |
|---|---|---|
| `ADAPTER_BOARD` | `gcp2` | `rp2040_zero` for this board |
| `ADAPTER_PORT_COUNT` | `4` | Number of GC ports, 1–4 |
| `ADAPTER_GC_PIN_BASE` | `26` | First GC data pin. Further ports use the following pins. |
| `ADAPTER_GENERIC_FLASH_BOOT` | `OFF` | Use the generic flash boot stage |
| `ADAPTER_LED_RGB_ORDER` | `OFF` | Turn on if the LED shows red and green swapped |

## Pins used

| RP2040-Zero pin | Use |
|---|---|
| GP26 | Port 1 data |
| GP27 | Port 2 data (4-port build only) |
| GP28 | Port 3 data (4-port build only) |
| GP29 | Port 4 data (4-port build only) |
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

That's the whole job for the 1-port build. For the 4-port build, repeat for
ports 2–4 on GP27, GP28 and GP29. All ports share 5V, 3V3 and GND.

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
- **Fit a 1 kΩ pull-up on every data pin the build uses**: GP26 only for the
  1-port build, GP26–GP29 for the 4-port build, even if a port stays empty.
  The self-test checks those lines at first boot and stops with an
  **orange** LED if one is missing.
- The chip's internal pull-up is far too weak for the controller signal. The
  external 1 kΩ is required.

### Safe bring-up order

1. Fit the 1 kΩ pull-up(s). Flash `tools/rp2040_zero_probe` and check that
   GP26 (and GP27–GP29 for 4 ports) shows **EXTERNAL PULL-UP** with a fast
   rise (well under 300 ns).
2. Connect **only GND, 3.3 V and DATA** from one controller. Press `j` in the
   probe. A reply of `09 00 ..` and live stick values mean these three wires
   are right. A controller works without the 5 V wire; it just can't rumble.
3. Add the **5 V** wire and press `j` again. The controller should rumble
   for 1 second.
4. Flash `gc_adapter_rp2040_zero_1port.uf2` (or the 4-port file).

## Buttons (optional but recommended)

Each button connects its pin to GND. Without buttons the adapter stays in its
default mode, Switch Pro Controller.

```
  GP11 ──── [button] ──── GND     (back)
  GP12 ──── [button] ──── GND     (forward)
```

- Use any momentary push button. No resistor is needed: the firmware turns
  on the chip's internal pull-ups, and it samples the buttons every 16 ms,
  which also removes contact bounce.
- On a 4-pin tactile switch, use two **diagonally opposite** legs. Legs on
  the same long side are already joined inside, so a button wired across
  them would read as always pressed.
- Mode order going forward: Switch Pro → XInput → GameCube adapter →
  Slippi → back to Switch Pro. The adapter reboots into the new mode.

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

With the 1-port build the adapter appears to the computer as a single
controller in Switch Pro and XInput modes. GameCube adapter and Slippi modes
always report 4 slots (the format Dolphin expects), and your controller uses
slot 1.

If yellow looks orange-ish and green looks red, your LED uses RGB order. Build
with `-DADAPTER_LED_RGB_ORDER=ON`, or use the probe's `l` test to confirm.

## Power

Everything runs from the USB port. Rumble motors draw from the 5V pin, so
several controllers rumbling at once need a port and cable that can supply
it. The board's 3.3 V regulator also powers the controllers' logic, which
is fine for four controllers.

### Capacitor on the rumble supply

When the rumble motor starts it draws a burst of current from the 5V pin.
That can pull the USB supply down for a moment and, in bad cases, reset the
board or corrupt a controller read. A capacitor across 5V and GND near the
controller supplies that burst locally.

```
  5V  ───┬──────────────► controller 5V (rumble)
         │ +
       [ C1 ]  47–100 µF electrolytic, 10 V or higher
         │ −
  GND ───┴──────────────► controller GND
         │
       [ C2 ]  100 nF ceramic, in parallel with C1 (optional)
```

- **C1, 47–100 µF electrolytic, rated 10 V or more.** The **stripe marks the
  negative leg**, which goes to GND; the longer leg is positive and goes to
  5V. Reversed, an electrolytic can overheat or burst.
- **C2, 100 nF ceramic (optional).** It has no polarity and handles the fast
  electrical noise the large capacitor is too slow for.
- Place both where the controller's 5V and GND wires join, with short leads.
- Don't go much above 100 µF. The USB standard limits the capacitance a
  device may put across USB 5V at plug-in (10 µF is the formal limit), because
  a big capacitor causes a current surge when you plug in. Most ports
  tolerate up to around 100 µF; much larger can cause USB dropouts.
- The motor and its driver sit inside the controller, so coil spikes are
  mostly contained there. On the adapter side, the main job of the capacitor
  is to stop the supply dip.
- Optional: a 10 µF capacitor from 3V3 to GND near the controller steadies
  the 3.3 V line too. Observe polarity if it is electrolytic or tantalum.
