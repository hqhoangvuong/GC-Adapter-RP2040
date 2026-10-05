#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H

#include "adapter_includes.h"

// The OLED settings menu. State changes happen on core 0; core 1 only
// reads it to draw the menu.

#define MENU_ITEM_COUNT 7

typedef struct
{
    const char *name;
    // Number of values, 0 for an action (Exit)
    uint8_t value_count;
    const char *const *values;
    // Setting this item edits, inside g_user_settings
    size_t offset;
} menu_item_s;

extern const menu_item_s g_menu_items[MENU_ITEM_COUNT];

bool menu_is_open();
uint8_t menu_selected();

// Name of the current value of an item, or NULL for an action
const char *menu_value_name(uint8_t item);

void menu_open();

// Close the menu, saving the settings if any changed
void menu_close();

// Move the selection by +1 or -1, wrapping
void menu_move(int step);

// Step the selected item's value by +1 or -1, wrapping. On Exit, closes.
void menu_change(int step);

// Close the menu after this long without input
#define MENU_TIMEOUT_US 60000000u

// Time of the last menu input, for the timeout
uint32_t menu_last_input();

#endif
