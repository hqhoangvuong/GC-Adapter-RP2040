#include "main.h"
#include "user_settings.h"

// Two sectors past the common settings (FLASH_TARGET_OFFSET + 1 sector in
// common/ll/adapter_ll_rp2040.c), so neither can overwrite the other
#define USER_SETTINGS_FLASH_OFFSET ((1200 * 1024) + (2 * FLASH_SECTOR_SIZE))

#define USER_SETTINGS_MAGIC   0x5A43474Eu   // "NGCZ"
#define USER_SETTINGS_VERSION 1

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    user_settings_s settings;
    uint32_t checksum;
} user_settings_store_s;

volatile user_settings_s g_user_settings;

static volatile bool _save_pending = false;

static const uint8_t _deadzone_radius[DEADZONE_COUNT] = {0, 5, 10, 15};

static uint32_t _checksum(const user_settings_s *s)
{
    const uint8_t *b = (const uint8_t *)s;
    uint32_t sum = 0x1234;
    for (size_t i = 0; i < sizeof(*s); i++)
        sum = (sum * 31) + b[i];
    return sum;
}

static void _defaults()
{
    g_user_settings.triggers = TRIGGERS_ANALOG;
    g_user_settings.deadzone = DEADZONE_OFF;
    g_user_settings.swap_abxy = 0;
    g_user_settings.brightness = BRIGHTNESS_MEDIUM;
#if defined(ADAPTER_OLED_FLIP)
    g_user_settings.flip = 1;
#else
    g_user_settings.flip = 0;
#endif
    g_user_settings.sleep = SLEEP_1MIN;
}

void user_settings_load()
{
    const user_settings_store_s *store =
        (const user_settings_store_s *)(XIP_BASE + USER_SETTINGS_FLASH_OFFSET);

    user_settings_s s = store->settings;
    bool ok = store->magic == USER_SETTINGS_MAGIC
              && store->version == USER_SETTINGS_VERSION
              && store->size == sizeof(user_settings_s)
              && store->checksum == _checksum(&s)
              && s.triggers < TRIGGERS_COUNT
              && s.deadzone < DEADZONE_COUNT
              && s.swap_abxy <= 1
              && s.brightness < BRIGHTNESS_LEVELS
              && s.flip <= 1
              && s.sleep < SLEEP_COUNT;

    if (ok)
        g_user_settings = s;
    else
        _defaults();
}

void user_settings_request_save()
{
    _save_pending = true;
}

bool user_settings_take_save(uint8_t *page, uint32_t *offset)
{
    if (!_save_pending)
        return false;
    _save_pending = false;

    user_settings_store_s store = {
        .magic = USER_SETTINGS_MAGIC,
        .version = USER_SETTINGS_VERSION,
        .size = sizeof(user_settings_s),
        .settings = g_user_settings,
    };
    store.checksum = _checksum(&store.settings);

    memset(page, 0xFF, FLASH_SECTOR_SIZE);
    memcpy(page, &store, sizeof(store));
    *offset = USER_SETTINGS_FLASH_OFFSET;
    return true;
}

static void _deadzone(uint8_t *x, uint8_t *y, int radius)
{
    int dx = (int)*x - 128;
    int dy = (int)*y - 128;
    if (dx * dx + dy * dy <= radius * radius)
    {
        *x = 128;
        *y = 128;
    }
}

void user_settings_apply_input(joybus_input_s *in)
{
    int radius = _deadzone_radius[g_user_settings.deadzone];
    if (radius)
    {
        _deadzone(&in->stick_left_x, &in->stick_left_y, radius);
        _deadzone(&in->stick_right_x, &in->stick_right_y, radius);
    }

    // Digital triggers: only the full click counts, as fully pressed. In
    // Switch mode a light press then no longer fires ZL/ZR.
    if (g_user_settings.triggers == TRIGGERS_DIGITAL)
    {
        in->analog_trigger_l = in->button_l ? 255 : 0;
        in->analog_trigger_r = in->button_r ? 255 : 0;
    }

    if (g_user_settings.swap_abxy)
    {
        uint8_t a = in->button_a, x = in->button_x;
        in->button_a = in->button_b;
        in->button_b = a;
        in->button_x = in->button_y;
        in->button_y = x;
    }
}
