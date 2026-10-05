#include "main.h"
#include "display.h"
#include "oled_ssd1306.h"
#include "joybus_status.h"
#include "hardware/sync.h"

// The OLED runs entirely on core 1. A full frame takes about 13 ms on the
// I2C bus, far longer than one controller poll, so it must stay off core 0.
// Core 1 only reads published state; core 0 never waits on it, except
// while saving settings to flash, for at most the frame in flight (see
// ui_button.c).

#define DISPLAY_FRAME_US   66000    // about 15 frames per second
#define DISPLAY_RETRY_US   1000000  // look for a missing display this often
#define DISPLAY_MESSAGE_US 1500000

// Burn-in protection: OLED pixels wear where they stay lit. After a minute
// with nothing happening (no button press, no controller plugged in or
// out, no input from the controller) the screen dims, and after ten
// minutes it switches off.
#define DISPLAY_DIM_US     (60u * 1000000u)
#define DISPLAY_OFF_US     (600u * 1000000u)

// Input changes smaller than this (stick noise) don't count as activity
#define DISPLAY_STICK_ACTIVITY   8
#define DISPLAY_TRIGGER_ACTIVITY 24

typedef enum
{
    BRIGHTNESS_FULL,
    BRIGHTNESS_DIM,
    BRIGHTNESS_OFF,
} brightness_t;

typedef enum
{
    MESSAGE_SAVED,
    MESSAGE_RECENTERED,
} message_t;

typedef enum
{
    SCREEN_LIVE,
    SCREEN_ORIGIN,
    SCREEN_STATUS,
    SCREEN_EVENTS,
    SCREEN_COUNT,
} screen_t;

extern uint32_t _adapter_rate;

static volatile bool _core1_ready = false;
static volatile bool _running = false;
static volatile bool _ready = false;
static volatile uint8_t _screen = SCREEN_LIVE;
static volatile int _fault_pin = -1;
static volatile uint32_t _message_start = 0;
static volatile bool _message_active = false;
static volatile input_mode_t _message_mode = INPUT_MODE_SWPRO;
static volatile message_t _message = MESSAGE_SAVED;
static volatile uint32_t _activity_time = 0;
static volatile bool _asleep = false;

static uint8_t _fb[OLED_FB_SIZE];

/* ---- 5x7 font, ASCII 0x20-0x7E, one byte per column, LSB on top ---- */

static const uint8_t _font[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14}, // space ! " #
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x56,0x20,0x50}, {0x00,0x00,0x07,0x00,0x00}, // $ % & '
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x2A,0x1C,0x7F,0x1C,0x2A}, {0x08,0x08,0x3E,0x08,0x08}, // ( ) * +
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02}, // , - . /
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, // 0 1 2 3
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, // 4 5 6 7
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00}, // 8 9 : ;
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, // < = > ?
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, // @ A B C
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A}, // D E F G
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, // H I J K
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, // L M N O
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, // P Q R S
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, // T U V W
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, // X Y Z [
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40}, // \ ] ^ _
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, // ` a b c
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E}, // d e f g
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00}, // h i j k
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, // l m n o
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, // p q r s
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C}, // t u v w
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, // x y z {
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08},                              // | } ~
};

/* ---- Drawing ---- */

static void _px(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    uint8_t *b = &_fb[(y / 8) * OLED_WIDTH + x];
    uint8_t m = 1u << (y % 8);
    *b = on ? (*b | m) : (*b & ~m);
}

static void _fill(int x, int y, int w, int h, bool on)
{
    for (int i = 0; i < w; i++)
        for (int j = 0; j < h; j++)
            _px(x + i, y + j, on);
}

static void _rect(int x, int y, int w, int h)
{
    for (int i = 0; i < w; i++)
    {
        _px(x + i, y, true);
        _px(x + i, y + h - 1, true);
    }
    for (int j = 0; j < h; j++)
    {
        _px(x, y + j, true);
        _px(x + w - 1, y + j, true);
    }
}

static void _char(int x, int y, char c, int scale, bool on)
{
    if (c < 0x20 || c > 0x7E)
        c = '?';
    const uint8_t *g = _font[c - 0x20];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 8; row++)
            if (g[col] & (1u << row))
                _fill(x + col * scale, y + row * scale, scale, scale, on);
}

// Draw text; each character takes 6 pixels across at scale 1
static void _text(int x, int y, const char *s, int scale, bool on)
{
    for (; *s; s++, x += 6 * scale)
        _char(x, y, *s, scale, on);
}

static int _text_width(const char *s, int scale)
{
    int n = strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

static void _text_right(int y, const char *s)
{
    _text(OLED_WIDTH - _text_width(s, 1), y, s, 1, true);
}

// A labelled key that turns solid while pressed
static void _key(int x, int y, int w, int h, const char *label, bool pressed)
{
    if (pressed)
        _fill(x, y, w, h, true);
    else
        _rect(x, y, w, h);
    int tx = x + (w - _text_width(label, 1)) / 2;
    int ty = y + (h - 7) / 2;
    _text(tx, ty, label, 1, !pressed);
}

// 30x30 box with a 3x3 dot for the stick position
static void _stick(int x, int y, uint8_t sx, uint8_t sy)
{
    _rect(x, y, 30, 30);
    // Centre mark
    _px(x + 14, y + 14, true);
    _px(x + 15, y + 15, true);
    _px(x + 14, y + 15, true);
    _px(x + 15, y + 14, true);
    // 26 dot positions across the 28-pixel interior; up is a larger value
    int dx = x + 2 + (sx * 25) / 255;
    int dy = y + 2 + ((255 - sy) * 25) / 255;
    _fill(dx - 1, dy - 1, 3, 3, true);
}

// Trigger bar with its digital button as the label on top
static void _trigger(int x, const char *label, uint8_t analog, bool digital)
{
    _key(x, 0, 9, 9, label, digital);
    _rect(x, 10, 9, 22);
    int h = (analog * 20) / 255;
    _fill(x + 1, 31 - h, 7, h, true);
}

/* ---- Screens ---- */

static const char *_mode_name(input_mode_t mode)
{
    switch (mode)
    {
    case INPUT_MODE_SWPRO:     return "SWITCH PRO";
    case INPUT_MODE_XINPUT:    return "XINPUT";
    case INPUT_MODE_GCADAPTER: return "GC ADAPTER";
    case INPUT_MODE_SLIPPI:    return "SLIPPI";
    default:                   return "?";
    }
}

static const char *_usb_state()
{
    if (tud_suspended())
        return "USB suspended";
    if (tud_mounted())
        return "USB connected";
    return "USB no host";
}

static void _rate_text(char *buf, size_t len)
{
    uint32_t rate = _adapter_rate ? _adapter_rate : 1;
    snprintf(buf, len, "%luHz", (unsigned long)((1000000 + rate / 2) / rate));
}

static void _screen_idle()
{
    const char *name = _mode_name(adapter_get_current_mode());
    // Large mode name, 12 pixels per character
    _text((OLED_WIDTH - _text_width(name, 2)) / 2, 0, name, 2, true);

    char rate[12];
    _rate_text(rate, sizeof(rate));
    _text(0, 17, _usb_state(), 1, true);
    _text_right(17, rate);
    _text(0, 25, "No controller", 1, true);
}

static void _screen_live(const joybus_port_status_s *st)
{
    const joybus_input_s *in = &st->input;

    _stick(0, 1, in->stick_left_x, in->stick_left_y);
    _stick(32, 1, in->stick_right_x, in->stick_right_y);

    _trigger(65, "L", in->analog_trigger_l, in->button_l);
    _trigger(76, "R", in->analog_trigger_r, in->button_r);

    // Face buttons: two rows of three
    _key(88,  0, 13, 10, "A", in->button_a);
    _key(101, 0, 13, 10, "B", in->button_b);
    _key(114, 0, 14, 10, "X", in->button_x);
    _key(88,  11, 13, 10, "Y", in->button_y);
    _key(101, 11, 13, 10, "Z", in->button_z);
    _key(114, 11, 14, 10, "S", in->button_start);

    // D-pad cross
    _key(93, 22, 4, 4, "", in->dpad_up);
    _key(93, 28, 4, 4, "", in->dpad_down);
    _key(89, 25, 4, 4, "", in->dpad_left);
    _key(97, 25, 4, 4, "", in->dpad_right);

    // Rumble, inverted while the host has it on
    _key(104, 22, 24, 10, "RMB", st->rumble);
}

static void _screen_origin(const joybus_port_status_s *st, uint port)
{
    char line[24];

    snprintf(line, sizeof(line), "P%u rest position", port + 1);
    _text(0, 0, line, 1, true);

    snprintf(line, sizeof(line), "Main %3u,%3u %+d,%+d",
             st->origin_lx, st->origin_ly, st->origin_lx - 128, st->origin_ly - 128);
    _text(0, 8, line, 1, true);

    snprintf(line, sizeof(line), "C    %3u,%3u %+d,%+d",
             st->origin_rx, st->origin_ry, st->origin_rx - 128, st->origin_ry - 128);
    _text(0, 16, line, 1, true);

    snprintf(line, sizeof(line), "Trig L %3u  R %3u", st->origin_lt, st->origin_rt);
    _text(0, 24, line, 1, true);
}

static void _screen_status(const joybus_port_status_s *st, uint32_t reads_per_s)
{
    char line[24];

    _text(0, 0, _mode_name(adapter_get_current_mode()), 1, true);
    _rate_text(line, sizeof(line));
    _text_right(0, line);

    _text(0, 8, _usb_state(), 1, true);

    snprintf(line, sizeof(line), "Reads %lu/s", (unsigned long)reads_per_s);
    _text(0, 16, line, 1, true);
    snprintf(line, sizeof(line), "Miss %lu", (unsigned long)st->misses);
    _text_right(16, line);

    uint32_t up = (time_us_32() - st->connect_time) / 1000000;
    snprintf(line, sizeof(line), "Drops %lu", (unsigned long)st->drops);
    _text(0, 24, line, 1, true);
    snprintf(line, sizeof(line), "Up %lu:%02lu", (unsigned long)(up / 60), (unsigned long)(up % 60));
    _text_right(24, line);
}

static const char *_event_name(uint8_t type)
{
    switch (type)
    {
    case JOYBUS_EVENT_CONNECT:     return "Connected";
    case JOYBUS_EVENT_DROP:        return "Dropped";
    case JOYBUS_EVENT_ORIGIN_ASK:  return "Origin asked";
    case JOYBUS_EVENT_ORIGIN_READ: return "Origin read";
    case JOYBUS_EVENT_ORIGIN_FAIL: return "Origin failed";
    case JOYBUS_EVENT_RECENTER:    return "Recentered";
    default:                       return NULL;
    }
}

// The last few controller events, for working out what happened
static void _screen_events(const joybus_port_status_s *st, uint port)
{
    char line[24];

    snprintf(line, sizeof(line), "P%u events", port + 1);
    _text(0, 0, line, 1, true);
    snprintf(line, sizeof(line), "Drops %lu", (unsigned long)st->drops);
    _text_right(0, line);

    uint32_t now = time_us_32();
    for (int i = 0; i < JOYBUS_EVENT_COUNT; i++)
    {
        const char *name = _event_name(st->event_type[i]);
        if (!name)
            break;

        int y = 8 + i * 8;
        _text(0, y, name, 1, true);

        uint32_t ago = (now - st->event_time[i]) / 1000000;
        if (ago < 60)
            snprintf(line, sizeof(line), "%lus", (unsigned long)ago);
        else if (ago < 3600)
            snprintf(line, sizeof(line), "%lum", (unsigned long)(ago / 60));
        else
            snprintf(line, sizeof(line), "%luh", (unsigned long)(ago / 3600));
        _text_right(y, line);
    }
}

static void _screen_saved(input_mode_t mode)
{
    _text((OLED_WIDTH - _text_width("SAVED", 2)) / 2, 0, "SAVED", 2, true);
    const char *name = _mode_name(mode);
    _text(0, 17, "Default mode:", 1, true);
    _text(0, 25, name, 1, true);
}

static void _screen_recentered()
{
    _text((OLED_WIDTH - _text_width("CENTERED", 2)) / 2, 0, "CENTERED", 2, true);
    _text(0, 17, "Stick center reset", 1, true);
    _text(0, 25, "by X+Y+Start", 1, true);
}

static void _show_message(message_t msg)
{
    _message = msg;
    _message_start = time_us_32();
    _message_active = true;
}

static void _screen_fault(int pin)
{
    char line[24];
    _text(0, 0, "SELF-TEST FAILED", 1, true);
    snprintf(line, sizeof(line), "No pull-up on GP%d", pin);
    _text(0, 8, line, 1, true);
    _text(0, 16, "Fit 1k from data", 1, true);
    _text(0, 24, "to 3V3, then replug", 1, true);
}

/* ---- Core 1 ---- */

// True if the controller input moved noticeably since the last time this
// returned true
static bool _input_activity(const joybus_input_s *in)
{
    static joybus_input_s last = {0};

    // Button bits live outside the stick bytes of byte_1
    bool buttons = ((in->byte_1 ^ last.byte_1) & 0xFFFF0000u) != 0;
    bool sticks = abs((int)in->stick_left_x - last.stick_left_x) > DISPLAY_STICK_ACTIVITY
               || abs((int)in->stick_left_y - last.stick_left_y) > DISPLAY_STICK_ACTIVITY
               || abs((int)in->stick_right_x - last.stick_right_x) > DISPLAY_STICK_ACTIVITY
               || abs((int)in->stick_right_y - last.stick_right_y) > DISPLAY_STICK_ACTIVITY
               || abs((int)in->analog_trigger_l - last.analog_trigger_l) > DISPLAY_TRIGGER_ACTIVITY
               || abs((int)in->analog_trigger_r - last.analog_trigger_r) > DISPLAY_TRIGGER_ACTIVITY;

    if (buttons || sticks)
    {
        last = *in;
        return true;
    }
    return false;
}

// Draw the current screen. Returns true if something happened that should
// keep the screen awake.
static bool _render(uint32_t reads_per_s)
{
    memset(_fb, 0, sizeof(_fb));

    if (_fault_pin >= 0)
    {
        _screen_fault(_fault_pin);
        return true;
    }

    if (!_ready)
    {
        _text(0, 12, "Starting...", 1, true);
        return true;
    }

    // Plugging a controller in or out counts as activity
    static bool was_connected = false;
    bool connected = false;

    // A new X+Y+Start reset on any port shows a confirmation
    static uint32_t recenters_seen = 0;
    uint32_t recenters = 0;
    joybus_port_status_s st;
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        joybus_itf_get_status(i, &st);
        recenters += st.recenters;
        connected |= (st.input.port_itf > -1);
    }
    bool active = (connected != was_connected);
    was_connected = connected;
    if (recenters != recenters_seen)
    {
        recenters_seen = recenters;
        _show_message(MESSAGE_RECENTERED);
    }

    if (_message_active)
    {
        if (time_us_32() - _message_start < DISPLAY_MESSAGE_US)
        {
            if (_message == MESSAGE_RECENTERED)
                _screen_recentered();
            else
                _screen_saved(_message_mode);
            return true;
        }
        _message_active = false;
    }

    // Show the first connected port
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        joybus_itf_get_status(i, &st);
        if (st.input.port_itf > -1)
        {
            switch (_screen)
            {
            default:
            case SCREEN_LIVE:   _screen_live(&st); break;
            case SCREEN_ORIGIN: _screen_origin(&st, i); break;
            case SCREEN_STATUS: _screen_status(&st, reads_per_s); break;
            case SCREEN_EVENTS: _screen_events(&st, i); break;
            }
            return _input_activity(&st.input) || active;
        }
    }

    _screen_idle();
    return active;
}

// Good reads per second on the first connected port
static uint32_t _measure_reads(uint32_t now)
{
    static uint32_t last_time = 0;
    static uint32_t last_reads = 0;
    static uint32_t rate = 0;

    if (now - last_time < 1000000)
        return rate;

    joybus_port_status_s st;
    uint32_t reads = 0;
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        joybus_itf_get_status(i, &st);
        if (st.input.port_itf > -1)
        {
            reads = st.reads;
            break;
        }
    }

    uint32_t elapsed = now - last_time;
    rate = (uint32_t)(((uint64_t)(reads - last_reads) * 1000000) / elapsed);
    last_reads = reads;
    last_time = now;
    return rate;
}

static void _core1_entry()
{
    // Let core 0 pause this core while it writes flash
    multicore_lockout_victim_init();
    _core1_ready = true;

    oled_bus_init();

    bool present = false;
    uint32_t last_try = time_us_32() - DISPLAY_RETRY_US;
    brightness_t brightness = BRIGHTNESS_FULL;
    bool active = false;
    _activity_time = time_us_32();

    for (;;)
    {
        uint32_t frame_start = time_us_32();

        uint32_t rate = _measure_reads(frame_start);

        if (present)
            active = _render(rate);

        // Interrupts stay off while bytes go out. That keeps core 0's
        // flash-save pause (an interrupt) from stopping this core in the
        // middle of a transfer; the pause waits for the frame to finish.
        uint32_t ints = save_and_disable_interrupts();

        if (!present && (frame_start - last_try >= DISPLAY_RETRY_US))
        {
            last_try = frame_start;
            present = oled_init();
            if (present)
            {
                // A fresh init leaves the panel on at full brightness
                brightness = BRIGHTNESS_FULL;
                active = _render(rate);
            }
        }

        if (present)
        {
            if (active)
                _activity_time = frame_start;

            uint32_t quiet = frame_start - _activity_time;
            brightness_t want = (quiet >= DISPLAY_OFF_US) ? BRIGHTNESS_OFF
                              : (quiet >= DISPLAY_DIM_US) ? BRIGHTNESS_DIM
                              : BRIGHTNESS_FULL;

            if (want != brightness)
            {
                bool ok = oled_set_dim(want != BRIGHTNESS_FULL)
                          && oled_set_on(want != BRIGHTNESS_OFF);
                if (ok)
                    brightness = want;
                present = ok;
            }

            if (present)
                present = oled_present(_fb);
        }

        // Only a display that is there can be asleep
        _asleep = present && (brightness != BRIGHTNESS_FULL);

        restore_interrupts(ints);

        while (time_us_32() - frame_start < DISPLAY_FRAME_US)
            tight_loop_contents();
    }
}

void display_start()
{
    multicore_launch_core1(_core1_entry);
    while (!_core1_ready)
        tight_loop_contents();
    _running = true;
}

bool display_running()
{
    return _running;
}

void display_set_ready()
{
    _ready = true;
}

void display_next_screen()
{
    _screen = (_screen + 1) % SCREEN_COUNT;
}

void display_show_saved(input_mode_t mode)
{
    _message_mode = mode;
    _show_message(MESSAGE_SAVED);
}

bool display_wake()
{
    bool was_asleep = _asleep;
    _activity_time = time_us_32();
    return was_asleep;
}

void display_set_fault_pullup(uint pin)
{
    _fault_pin = (int)pin;
}
