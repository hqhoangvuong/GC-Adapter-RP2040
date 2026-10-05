#include "main.h"
#include "settings_menu.h"
#include "user_settings.h"
#include "display.h"

static const char *const _off_on[] = {"Off", "On"};
static const char *const _triggers[TRIGGERS_COUNT] = {"Analog", "Digital"};
static const char *const _deadzones[DEADZONE_COUNT] = {"Off", "5", "10", "15"};
static const char *const _brightness[BRIGHTNESS_LEVELS] = {"Low", "Medium", "High"};
static const char *const _sleep[SLEEP_COUNT] = {"1 min", "5 min", "Never"};

#define FIELD(name) offsetof(user_settings_s, name)

const menu_item_s g_menu_items[MENU_ITEM_COUNT] = {
    {"Triggers",     TRIGGERS_COUNT,    _triggers,   FIELD(triggers)},
    {"Deadzone",     DEADZONE_COUNT,    _deadzones,  FIELD(deadzone)},
    {"Swap A/B X/Y", 2,                 _off_on,     FIELD(swap_abxy)},
    {"Brightness",   BRIGHTNESS_LEVELS, _brightness, FIELD(brightness)},
    {"Flip screen",  2,                 _off_on,     FIELD(flip)},
    {"Screen sleep", SLEEP_COUNT,       _sleep,      FIELD(sleep)},
    {"Save & exit",  0,                 NULL,        0},
};

static volatile bool _open = false;
static volatile uint8_t _selected = 0;
static bool _dirty = false;
static uint32_t _last_input = 0;

static volatile uint8_t *_field(uint8_t item)
{
    return ((volatile uint8_t *)&g_user_settings) + g_menu_items[item].offset;
}

bool menu_is_open()
{
    return _open;
}

uint8_t menu_selected()
{
    return _selected;
}

uint32_t menu_last_input()
{
    return _last_input;
}

const char *menu_value_name(uint8_t item)
{
    if (item >= MENU_ITEM_COUNT || !g_menu_items[item].value_count)
        return NULL;
    uint8_t v = *_field(item);
    if (v >= g_menu_items[item].value_count)
        v = 0;
    return g_menu_items[item].values[v];
}

void menu_open()
{
    _selected = 0;
    _dirty = false;
    _last_input = time_us_32();
    _open = true;
}

void menu_close()
{
    if (!_open)
        return;
    _open = false;
    if (_dirty)
    {
        user_settings_request_save();
        display_show_settings_saved();
    }
    _dirty = false;
}

void menu_move(int step)
{
    _last_input = time_us_32();
    _selected = (uint8_t)((_selected + MENU_ITEM_COUNT + step) % MENU_ITEM_COUNT);
}

void menu_change(int step)
{
    _last_input = time_us_32();

    const menu_item_s *item = &g_menu_items[_selected];
    if (!item->value_count)
    {
        menu_close();
        return;
    }

    volatile uint8_t *f = _field(_selected);
    *f = (uint8_t)((*f + item->value_count + step) % item->value_count);
    _dirty = true;
}
