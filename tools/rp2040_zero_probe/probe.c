// RP2040-Zero (and clone) discovery probe for the GC adapter firmware.
//
// Flash the .uf2, open the USB serial port (any baud), and read the report.
// It answers the board-specific unknowns the adapter firmware depends on:
//   - flash chip vendor and real size (the adapter stores settings at 1.2 MB)
//   - crystal frequency and chip revision
//   - which GPIOs have external pull-ups (GC data lines need ~1k to 3V3)
//   - how fast each pulled-up line rises (is the pull-up strong enough)
//   - whether a GameCube controller answers on those lines, and rumble
//   - which pins your mode buttons are on (live monitor)
//   - the onboard RGB LED colour order (GRB vs RGB)

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "ws2812.pio.h"

#define LED_PIN     16
#define GPIO_COUNT  30

// Where the adapter firmware keeps its settings (FW/common/ll/adapter_ll_rp2040.c)
#define HHL_SETTINGS_OFFSET ((1200 * 1024) + 4096)
#define HHL_SETTINGS_VERSION 0x0003

// Pin classification results, filled by scan_pins()
typedef enum
{
    PIN_FLOATING,  // follows the internal pull: nothing strong attached
    PIN_EXT_HIGH,  // stays high against the internal pull-down: external pull-up
    PIN_EXT_LOW,   // stays low against the internal pull-up: tied to GND / button held
    PIN_ODD,       // reads inverted: something is driving it
    PIN_SKIPPED,
} pin_class_t;

static pin_class_t _pin_class[GPIO_COUNT];
static uint32_t _rise_ns[GPIO_COUNT];
static bool _rise_timeout[GPIO_COUNT];
static bool _gc_found[GPIO_COUNT];

static uint32_t _cyc_per_us = 125;

static PIO _led_pio = pio0;
static uint _led_sm = 0;
static bool _led_ready = false;

//--------------------------------------------------------------------+
// SysTick cycle timer (24 bit, counts down at clk_sys)
//--------------------------------------------------------------------+

static inline void systick_start(void)
{
    systick_hw->rvr = 0x00FFFFFF;
    systick_hw->cvr = 0;
    systick_hw->csr = 0x5; // enable, processor clock, no interrupt
}

static inline __attribute__((always_inline)) uint32_t st_now(void)
{
    return systick_hw->cvr;
}

static inline __attribute__((always_inline)) uint32_t st_elapsed(uint32_t from)
{
    return (from - systick_hw->cvr) & 0x00FFFFFF;
}

//--------------------------------------------------------------------+
// LED
//--------------------------------------------------------------------+

static void led_init(void)
{
    uint offset = pio_add_program(_led_pio, &ws2812_program);
    ws2812_program_init(_led_pio, _led_sm, offset, LED_PIN, false);
    _led_ready = true;
}

// Sends three raw bytes to the LED, first byte first
static void led_raw(uint8_t b0, uint8_t b1, uint8_t b2)
{
    if (!_led_ready) return;
    pio_sm_put_blocking(_led_pio, _led_sm, ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8));
}

//--------------------------------------------------------------------+
// Chip, clocks, flash
//--------------------------------------------------------------------+

static const char *flash_vendor(uint8_t id)
{
    switch (id)
    {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0x85: return "Puya";
    case 0x5E: return "Zbit";
    case 0x68: return "Boya";
    case 0x0B: return "XTX";
    case 0x20: return "XMC or Micron";
    case 0x9D: return "ISSI";
    case 0x1F: return "Adesto";
    case 0xC2: return "Macronix";
    case 0x1C: return "EON";
    default:   return "unknown";
    }
}

static void __no_inline_not_in_flash_func(read_jedec)(uint8_t *out)
{
    uint8_t tx[4] = {0x9F, 0, 0, 0};
    uint8_t rx[4] = {0};
    uint32_t ints = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, 4);
    restore_interrupts(ints);
    memcpy(out, &rx[1], 3);
}

// Finds the real flash size by looking for where the XIP window wraps
// back to offset 0. Returns 0 if no wrap was found below 16 MB.
static uint32_t flash_alias_size(void)
{
    const uint8_t *base = (const uint8_t *)XIP_NOCACHE_NOALLOC_BASE;
    for (uint32_t size = 256 * 1024; size < 16 * 1024 * 1024; size <<= 1)
    {
        if (memcmp(base, base + size, 256) == 0) return size;
    }
    return 0;
}

static void report_chip(void)
{
    printf("\n=== Chip & clocks ===\n");
    printf("RP2040 chip version : %u (1 = B0/B1, 2 = B2)\n", rp2040_chip_version());
    printf("Boot ROM version    : %u\n", rp2040_rom_version());

    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    printf("Board unique ID     : ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) printf("%02X", id.id[i]);
    printf("\n");

    uint32_t xosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC);
    printf("Crystal (XOSC)      : %lu kHz %s\n", (unsigned long)xosc,
           (xosc > 11950 && xosc < 12050) ? "(OK, 12 MHz)" : "(UNEXPECTED, firmware assumes 12 MHz)");
    printf("clk_sys             : %lu kHz\n", (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));
    printf("clk_usb             : %lu kHz\n", (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB));
}

static void report_flash(void)
{
    printf("\n=== Flash ===\n");
    uint8_t jedec[3];
    read_jedec(jedec);
    printf("JEDEC ID            : %02X %02X %02X (vendor: %s)\n", jedec[0], jedec[1], jedec[2], flash_vendor(jedec[0]));

    uint32_t claimed = 0;
    if (jedec[2] >= 0x10 && jedec[2] <= 0x19) claimed = 1u << jedec[2];
    if (claimed) printf("Size claimed by ID  : %lu KB\n", (unsigned long)(claimed / 1024));
    else         printf("Size claimed by ID  : (not decodable)\n");

    uint32_t real = flash_alias_size();
    if (real) printf("Size by wrap test   : %lu KB\n", (unsigned long)(real / 1024));
    else      printf("Size by wrap test   : 16 MB (no wrap found)\n");

    if (claimed && real && claimed != real)
        printf("  !! ID and wrap test disagree - the chip may be relabelled; trust the wrap test.\n");

    uint32_t size = real ? real : (16u * 1024 * 1024);
    if (size >= HHL_SETTINGS_OFFSET + 4096)
        printf("Adapter settings at 1204 KB: fits inside the chip (OK)\n");
    else
        printf("Adapter settings at 1204 KB: BEYOND the chip, wraps to %lu KB (needs a firmware change)\n",
               (unsigned long)((HHL_SETTINGS_OFFSET % size) / 1024));

    const uint8_t *s = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + (HHL_SETTINGS_OFFSET % size));
    uint16_t ver = s[0] | (s[1] << 8);
    if (ver == HHL_SETTINGS_VERSION)
        printf("Existing adapter settings found (version %u)\n", ver);
    else if (ver == 0xFFFF)
        printf("Adapter settings area is erased (adapter firmware never saved here)\n");
    else
        printf("Adapter settings area holds other data (first word 0x%04X)\n", ver);

    if (jedec[0] == 0xEF || jedec[0] == 0xC8)
        printf("Boot stage 2        : the adapter's default (W25Q080) is known to suit this vendor\n");
    else
        printf("Boot stage 2        : non-Winbond/GigaDevice flash - if the adapter .uf2 does not boot,\n"
               "                      build it with set(PICO_DEFAULT_BOOT_STAGE2 boot2_generic_03h)\n");
}

//--------------------------------------------------------------------+
// Pin scan
//--------------------------------------------------------------------+

// Drives the pin low briefly, releases it, and times how long the external
// pull-up takes to bring it back high.
static uint32_t __no_inline_not_in_flash_func(measure_rise_cycles)(uint pin, bool *timeout)
{
    const uint32_t mask = 1u << pin;
    const uint32_t limit = 20 * _cyc_per_us;

    gpio_disable_pulls(pin);
    sio_hw->gpio_clr = mask;
    sio_hw->gpio_oe_set = mask;
    busy_wait_us_32(5);

    uint32_t ints = save_and_disable_interrupts();
    uint32_t t0 = st_now();
    sio_hw->gpio_oe_clr = mask;
    uint32_t cyc;
    while (!(sio_hw->gpio_in & mask))
    {
        if (st_elapsed(t0) > limit) break;
    }
    cyc = st_elapsed(t0);
    restore_interrupts(ints);

    *timeout = cyc > limit;
    return cyc;
}

static void scan_pins(void)
{
    for (uint p = 0; p < GPIO_COUNT; p++)
    {
        _rise_timeout[p] = false;
        _rise_ns[p] = 0;

        if (p == LED_PIN)
        {
            _pin_class[p] = PIN_SKIPPED;
            continue;
        }

        gpio_init(p);
        gpio_set_pulls(p, false, true);
        sleep_us(200);
        bool with_down = gpio_get(p);
        gpio_set_pulls(p, true, false);
        sleep_us(200);
        bool with_up = gpio_get(p);

        if (!with_down && with_up)      _pin_class[p] = PIN_FLOATING;
        else if (with_down && with_up)  _pin_class[p] = PIN_EXT_HIGH;
        else if (!with_down && !with_up) _pin_class[p] = PIN_EXT_LOW;
        else                            _pin_class[p] = PIN_ODD;

        if (_pin_class[p] == PIN_EXT_HIGH)
        {
            bool to;
            uint32_t cyc = measure_rise_cycles(p, &to);
            _rise_timeout[p] = to;
            _rise_ns[p] = (cyc * 1000) / _cyc_per_us;
        }

        // Back to the reset state (input, pull-down)
        gpio_set_pulls(p, false, true);
    }
}

static const char *rise_verdict(uint p)
{
    if (_rise_timeout[p]) return "did not rise within 20 us - not a pull-up (driven or shorted?)";
    if (_rise_ns[p] < 300) return "fast - strong pull-up, good for GC data";
    if (_rise_ns[p] < 1000) return "slow - pull-up is marginal for GC data";
    return "too slow - pull-up too weak for GC data";
}

static void report_pins(void)
{
    printf("\n=== GPIO scan (GP%u = onboard LED, skipped) ===\n", LED_PIN);
    printf("Only internal pulls are used, except pins with an external pull-up,\n");
    printf("which are pulled low for 5 us to time the rise.\n\n");

    for (uint p = 0; p < GPIO_COUNT; p++)
    {
        switch (_pin_class[p])
        {
        case PIN_FLOATING:
            printf("GP%-2u: floating (nothing strong attached)\n", p);
            break;
        case PIN_EXT_HIGH:
            printf("GP%-2u: EXTERNAL PULL-UP, rise %lu ns: %s\n", p, (unsigned long)_rise_ns[p], rise_verdict(p));
            break;
        case PIN_EXT_LOW:
            printf("GP%-2u: HELD LOW (tied to GND, or a button is pressed)\n", p);
            break;
        case PIN_ODD:
            printf("GP%-2u: odd reading (actively driven?)\n", p);
            break;
        default:
            break;
        }
    }
}

//--------------------------------------------------------------------+
// Joybus (GameCube controller protocol), bit-banged
//--------------------------------------------------------------------+

// Sends tx_len bytes plus a stop bit on an open-drain line, then records the
// reply. Returns the number of bits received (including the stop bit).
static int __no_inline_not_in_flash_func(joybus_transfer)(uint pin, const uint8_t *tx, int tx_len, uint8_t *rx, int rx_max)
{
    const uint32_t mask = 1u << pin;
    const uint32_t us = _cyc_per_us;
    int nbits = 0;

    memset(rx, 0, rx_max);

    sio_hw->gpio_clr = mask;
    sio_hw->gpio_oe_clr = mask;

    uint32_t ints = save_and_disable_interrupts();

    uint32_t t = st_now();
    for (int i = 0; i < tx_len * 8; i++)
    {
        bool one = (tx[i / 8] >> (7 - (i % 8))) & 1;
        uint32_t low = one ? us : 3 * us;

        sio_hw->gpio_oe_set = mask;
        while (st_elapsed(t) < low) {}
        sio_hw->gpio_oe_clr = mask;
        while (st_elapsed(t) < 4 * us) {}
        t = (t - 4 * us) & 0x00FFFFFF;
    }

    // Stop bit: 1 us low, then release and listen straight away, since the
    // controller may start replying before a full bit period has passed
    sio_hw->gpio_oe_set = mask;
    while (st_elapsed(t) < us) {}
    sio_hw->gpio_oe_clr = mask;

    uint32_t w = st_now();
    while (!(sio_hw->gpio_in & mask))
    {
        if (st_elapsed(w) > 2 * us) goto done;
    }

    // Wait for the reply to start
    while (sio_hw->gpio_in & mask)
    {
        if (st_elapsed(w) > 200 * us) goto done;
    }

    while (nbits < rx_max * 8 + 1)
    {
        uint32_t e = st_now();
        while (st_elapsed(e) < 2 * us) {}
        bool bit = sio_hw->gpio_in & mask;
        if (bit && nbits < rx_max * 8) rx[nbits / 8] |= 1 << (7 - (nbits % 8));
        nbits++;

        while (!(sio_hw->gpio_in & mask))
        {
            if (st_elapsed(e) > 6 * us) goto done;
        }
        while (sio_hw->gpio_in & mask)
        {
            if (st_elapsed(e) > 8 * us) goto done;
        }
    }

done:
    restore_interrupts(ints);
    return nbits;
}

static const char *gc_device_name(uint8_t b0, uint8_t b1)
{
    if (b0 == 0x09 && b1 == 0x00) return "GameCube controller";
    if (b0 == 0x09) return "GameCube controller (non-standard ID byte 2)";
    if ((b0 & 0xE0) == 0xA0 || (b0 & 0xE0) == 0xE0) return "WaveBird receiver";
    if (b0 == 0x05) return "N64 controller";
    return "unknown device";
}

static void print_gc_state(const uint8_t *r)
{
    printf("  A%u B%u X%u Y%u St%u Z%u L%u R%u  Stick %3u,%3u  C %3u,%3u  LT %3u RT %3u\n",
           r[0] & 1, (r[0] >> 1) & 1, (r[0] >> 2) & 1, (r[0] >> 3) & 1, (r[0] >> 4) & 1,
           (r[1] >> 4) & 1, (r[1] >> 6) & 1, (r[1] >> 5) & 1,
           r[2], r[3], r[4], r[5], r[6], r[7]);
}

static void joybus_probe_pin(uint p, bool full)
{
    uint8_t rx[10];
    const uint8_t probe[1] = {0x00};

    gpio_init(p);
    gpio_disable_pulls(p);

    int n = joybus_transfer(p, probe, 1, rx, 3);
    if (n < 25)
    {
        printf("GP%-2u: no controller reply (%d bits)\n", p, n);
        _gc_found[p] = false;
        gpio_set_pulls(p, false, true);
        return;
    }

    _gc_found[p] = true;
    printf("GP%-2u: reply %02X %02X %02X -> %s\n", p, rx[0], rx[1], rx[2], gc_device_name(rx[0], rx[1]));

    if (!full)
    {
        gpio_set_pulls(p, false, true);
        return;
    }

    const uint8_t origin[1] = {0x41};
    sleep_ms(2);
    n = joybus_transfer(p, origin, 1, rx, 10);
    if (n >= 64)
    {
        printf("  Origin (rest position):\n");
        print_gc_state(rx);
    }

    printf("  Live input for 3 s - move sticks, press buttons:\n");
    const uint8_t poll[3] = {0x40, 0x03, 0x00};
    for (int i = 0; i < 300; i++)
    {
        n = joybus_transfer(p, poll, 3, rx, 8);
        if ((i % 25) == 0)
        {
            if (n >= 64) print_gc_state(rx);
            else printf("  (poll failed, %d bits)\n", n);
        }
        sleep_ms(10);
    }

    printf("  Rumble for 1 s - did it vibrate? (needs the 5V line wired)\n");
    const uint8_t rumble_on[3] = {0x40, 0x03, 0x01};
    for (int i = 0; i < 100; i++)
    {
        joybus_transfer(p, rumble_on, 3, rx, 8);
        sleep_ms(10);
    }
    joybus_transfer(p, poll, 3, rx, 8);

    gpio_set_pulls(p, false, true);
}

static void report_joybus(bool full)
{
    printf("\n=== GameCube controller scan (pins with an external pull-up) ===\n");
    bool any_candidate = false;
    for (uint p = 0; p < GPIO_COUNT; p++)
    {
        _gc_found[p] = false;
        if (_pin_class[p] != PIN_EXT_HIGH || _rise_timeout[p]) continue;
        any_candidate = true;
        joybus_probe_pin(p, full);
    }
    if (!any_candidate)
        printf("No pin has an external pull-up, so no GC data line can work yet.\n"
               "Add ~1k from each data line to 3V3, then rerun.\n");
}

//--------------------------------------------------------------------+
// Summary
//--------------------------------------------------------------------+

static void report_summary(void)
{
    printf("\n=== Summary (copy everything from here to the end) ===\n");

    uint8_t jedec[3];
    read_jedec(jedec);
    uint32_t real = flash_alias_size();
    printf("flash: %02X%02X%02X %s, %lu KB\n", jedec[0], jedec[1], jedec[2], flash_vendor(jedec[0]),
           (unsigned long)((real ? real : 16u * 1024 * 1024) / 1024));
    printf("xosc: %lu kHz\n", (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC));

    printf("pull-ups:");
    for (uint p = 0; p < GPIO_COUNT; p++)
        if (_pin_class[p] == PIN_EXT_HIGH) printf(" GP%u(%luns%s)", p, (unsigned long)_rise_ns[p], _rise_timeout[p] ? ",timeout" : "");
    printf("\n");

    printf("held low:");
    for (uint p = 0; p < GPIO_COUNT; p++)
        if (_pin_class[p] == PIN_EXT_LOW) printf(" GP%u", p);
    printf("\n");

    printf("controllers:");
    for (uint p = 0; p < GPIO_COUNT; p++)
        if (_gc_found[p]) printf(" GP%u", p);
    printf("\n");

    // The adapter's PIO setup needs four consecutive data pins
    int run_start = -1;
    for (int p = 0; p + 3 < GPIO_COUNT; p++)
    {
        bool ok = true;
        for (int k = 0; k < 4; k++)
        {
            if (_pin_class[p + k] != PIN_EXT_HIGH || _rise_timeout[p + k]) ok = false;
        }
        if (ok)
        {
            run_start = p;
            break;
        }
    }
    if (run_start >= 0)
        printf("data pins: 4 consecutive pulled-up pins GP%d-GP%d -> JOYBUS_PORT_1 = %d\n", run_start, run_start + 3, run_start);
    else
        printf("data pins: no 4 consecutive pulled-up pins yet (the adapter needs GPn..GPn+3)\n");
}

//--------------------------------------------------------------------+
// Interactive tools
//--------------------------------------------------------------------+

static void led_test(void)
{
    printf("\n=== LED test (GP%u) ===\n", LED_PIN);
    printf("Three steps, 2 s each. Note the colour you see at each step.\n");
    const char *names[3] = {"first byte", "second byte", "third byte"};
    for (int i = 0; i < 3; i++)
    {
        printf("  Step %d (%s only)...\n", i + 1, names[i]);
        led_raw(i == 0 ? 0x40 : 0, i == 1 ? 0x40 : 0, i == 2 ? 0x40 : 0);
        sleep_ms(2000);
    }
    led_raw(0, 0, 0);
    printf("Step colours GREEN, RED, BLUE  -> GRB order (matches the adapter firmware)\n");
    printf("Step colours RED, GREEN, BLUE  -> RGB order (adapter colours will show red/green swapped)\n");
    printf("Nothing lit                    -> no WS2812 on GP%u\n", LED_PIN);
}

static void pin_monitor(void)
{
    printf("\n=== Pin monitor: press buttons / plug controllers. Any key to stop. ===\n");
    printf("(internal pull-ups on; a button wired to GND reads 0 when pressed)\n");

    bool last[GPIO_COUNT];
    for (uint p = 0; p < GPIO_COUNT; p++)
    {
        if (p == LED_PIN) continue;
        gpio_init(p);
        gpio_set_pulls(p, true, false);
    }
    sleep_ms(5);
    for (uint p = 0; p < GPIO_COUNT; p++) last[p] = (p == LED_PIN) ? 0 : gpio_get(p);

    while (getchar_timeout_us(0) == PICO_ERROR_TIMEOUT)
    {
        for (uint p = 0; p < GPIO_COUNT; p++)
        {
            if (p == LED_PIN) continue;
            bool v = gpio_get(p);
            if (v != last[p])
            {
                printf("GP%-2u: %u -> %u%s\n", p, last[p], v, v ? "" : "  (pressed / pulled low)");
                last[p] = v;
            }
        }
        sleep_ms(5);
    }

    for (uint p = 0; p < GPIO_COUNT; p++)
    {
        if (p == LED_PIN) continue;
        gpio_set_pulls(p, false, true);
    }
    printf("Monitor stopped.\n");
}

static void full_report(bool full_joybus)
{
    printf("\n\n##### RP2040-Zero probe for GC-Adapter-RP2040 #####\n");
    report_chip();
    report_flash();
    scan_pins();
    report_pins();
    report_joybus(full_joybus);
    report_summary();
}

static void print_menu(void)
{
    printf("\nCommands:\n");
    printf("  r  rerun the report\n");
    printf("  j  controller test (live input + rumble) on pulled-up pins\n");
    printf("  l  LED colour-order test\n");
    printf("  m  live pin monitor (find your button pins)\n");
    printf("  b  reboot into BOOTSEL (to flash other firmware)\n");
}

int main(void)
{
    stdio_init_all();
    systick_start();
    _cyc_per_us = clock_get_hz(clk_sys) / 1000000;
    led_init();

    // Dim white breathing while waiting for the serial port to open
    uint8_t level = 0;
    int dir = 1;
    while (!stdio_usb_connected())
    {
        led_raw(level, level, level);
        level += dir;
        if (level == 0 || level == 24) dir = -dir;
        sleep_ms(40);
    }
    led_raw(0, 0, 0);
    sleep_ms(500);

    full_report(false);
    print_menu();

    for (;;)
    {
        int c = getchar_timeout_us(100000);
        switch (c)
        {
        case 'r': full_report(false); print_menu(); break;
        case 'j': scan_pins(); report_joybus(true); report_summary(); print_menu(); break;
        case 'l': led_test(); print_menu(); break;
        case 'm': pin_monitor(); print_menu(); break;
        case 'b': printf("Rebooting to BOOTSEL...\n"); sleep_ms(100); reset_usb_boot(0, 0); break;
        default: break;
        }
    }
}
