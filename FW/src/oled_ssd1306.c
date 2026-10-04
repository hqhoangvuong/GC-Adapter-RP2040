#include "main.h"
#include "hardware/i2c.h"
#include "oled_ssd1306.h"

// Runs on core 1 only. Every transfer has a timeout, so a missing or
// unplugged display never hangs the core.

#define OLED_I2C_HZ 400000
#define OLED_CTRL_CMD  0x00
#define OLED_CTRL_DATA 0x40

static uint8_t _oled_addr = 0x3C;

static bool _oled_write(const uint8_t *buf, size_t len)
{
    // Allow about twice the time the bytes need at 400 kHz
    uint32_t timeout = 1000 + len * 50;
    if (i2c_write_timeout_us(OLED_I2C, _oled_addr, buf, len, false, timeout) == (int)len)
        return true;

    // On a timeout the SDK returns without sending STOP, which leaves the
    // display in the middle of a transfer: the next frame's bytes would land
    // as pixel data in the wrong place. Resetting the I2C block frees the
    // bus, and the next START makes the display listen afresh.
    i2c_init(OLED_I2C, OLED_I2C_HZ);
    return false;
}

static bool _oled_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buf[32];
    if (len + 1 > sizeof(buf))
        return false;
    buf[0] = OLED_CTRL_CMD;
    memcpy(&buf[1], cmds, len);
    return _oled_write(buf, len + 1);
}

void oled_bus_init()
{
    i2c_init(OLED_I2C, OLED_I2C_HZ);
    gpio_set_function(ADAPTER_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(ADAPTER_OLED_SCL, GPIO_FUNC_I2C);
    // Modules carry their own pull-ups. These weak ones only stop the
    // lines floating when no module is fitted.
    gpio_pull_up(ADAPTER_OLED_SDA);
    gpio_pull_up(ADAPTER_OLED_SCL);
}

bool oled_init()
{
    static const uint8_t addrs[2] = {0x3C, 0x3D};
    bool found = false;

    for (uint i = 0; i < 2 && !found; i++)
    {
        _oled_addr = addrs[i];
        // A lone command byte with no command is harmless
        uint8_t probe = OLED_CTRL_CMD;
        found = _oled_write(&probe, 1);
    }
    if (!found)
        return false;

    static const uint8_t init[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock divide
        0xA8, 0x1F, // multiplex: 32 rows
        0xD3, 0x00, // no display offset
        0x40,       // start line 0
        0x8D, 0x14, // charge pump on
        0x20, 0x00, // horizontal addressing
#if defined(ADAPTER_OLED_FLIP)
        0xA0, 0xC0, // segment and COM scan not remapped (rotated 180)
#else
        0xA1, 0xC8, // segment remap, COM scan from the bottom
#endif
        0xDA, 0x02, // COM pins for 128x32
        0x81, OLED_CONTRAST_FULL, // contrast
        0xD9, 0xF1, // precharge
        0xDB, 0x40, // VCOMH level
        0x2E,       // scrolling off
        0xA4,       // show RAM contents
        0xA6,       // normal, not inverted
        0xAF,       // display on
    };
    return _oled_cmds(init, sizeof(init));
}

bool oled_set_contrast(uint8_t contrast)
{
    uint8_t cmd[2] = {0x81, contrast};
    return _oled_cmds(cmd, sizeof(cmd));
}

bool oled_set_on(bool on)
{
    uint8_t cmd = on ? 0xAF : 0xAE;
    return _oled_cmds(&cmd, 1);
}

bool oled_present(const uint8_t *fb)
{
    static const uint8_t window[] = {
        0x21, 0, OLED_WIDTH - 1,            // columns
        0x22, 0, (OLED_HEIGHT / 8) - 1,     // pages
    };
    if (!_oled_cmds(window, sizeof(window)))
        return false;

    static uint8_t buf[OLED_FB_SIZE + 1];
    buf[0] = OLED_CTRL_DATA;
    memcpy(&buf[1], fb, OLED_FB_SIZE);
    return _oled_write(buf, sizeof(buf));
}
