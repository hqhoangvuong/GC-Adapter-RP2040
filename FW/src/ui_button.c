#include "main.h"
#include "display.h"
#include "ui_button.h"
#include "joybus_status.h"
#include "user_settings.h"
#include "settings_menu.h"
#include "hardware/sync.h"

// One button, wired from ADAPTER_BUTTON_PIN to GND:
//   No controller:        click = next mode (the adapter reboots into it)
//                         hold  = save the current mode as the default
//   Controller connected: click = next OLED screen
//                         hold on the stick range screen = clear the test
//                         hold on the settings screen = open the menu
//   Settings menu open:   click = next item, hold = change its value
//                         (or use the controller, see _ui_menu_task)
//   Screen dimmed or off: click = wake it only
// Holding it while plugging in USB still enters BOOTSEL.

#define UI_SAMPLE_US     10000
#define UI_STABLE_COUNT  2         // samples in a row before a change counts
#define UI_LONG_PRESS_US 1000000

// From the common code
extern volatile bool _save_flag;
extern adapter_settings_s *_mem_settings_ptr;
extern joybus_input_s *_adapter_joybus_inputs;
void adapter_comms_task(uint32_t timestamp);

static bool _any_connected()
{
    if (!_adapter_joybus_inputs)
        return false;

    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        if (_adapter_joybus_inputs[i].port_itf > -1)
            return true;
    }
    return false;
}

static void _ui_button_task(uint32_t timestamp)
{
    static interval_s sample_state = {0};
    static bool raw_last = false;
    static uint8_t raw_count = 0;
    static bool pressed = false;
    static bool long_done = false;
    static bool connected_at_press = false;
    static bool wake_only = false;
    static uint32_t press_time = 0;

    if (!interval_run(timestamp, UI_SAMPLE_US, &sample_state))
        return;

    // Debounce: a level has to hold for a few samples
    bool raw = !adapter_ll_gpio_read(ADAPTER_BUTTON_1);
    if (raw != raw_last)
    {
        raw_last = raw;
        raw_count = 0;
    }
    else if (raw_count < UI_STABLE_COUNT)
    {
        raw_count++;
    }

    bool level = (raw_count >= UI_STABLE_COUNT) ? raw : pressed;

    if (level && !pressed)
    {
        pressed = true;
        long_done = false;
        press_time = timestamp;
        connected_at_press = _any_connected();
        // A click on a dimmed or dark screen only wakes it, so you can see
        // the mode before changing it
        wake_only = display_wake();
    }
    else if (level && pressed)
    {
        bool held = !long_done && (timestamp - press_time >= UI_LONG_PRESS_US);

        if (held && menu_is_open())
        {
            long_done = true;
            menu_change(+1);
        }
        // Save as soon as the hold is long enough, so the screen confirms
        // it while the button is still down
        else if (!long_done && !connected_at_press && !_any_connected()
            && (timestamp - press_time >= UI_LONG_PRESS_US))
        {
            long_done = true;
            settings_set_mode(adapter_get_current_mode());
            settings_save();
            display_show_saved(adapter_get_current_mode());
        }
        // On the stick range screen a hold starts the test again
        else if (!long_done && connected_at_press && _any_connected()
                 && display_on_range_screen()
                 && (timestamp - press_time >= UI_LONG_PRESS_US))
        {
            long_done = true;
            joybus_itf_reset_range();
        }
        else if (held && connected_at_press && _any_connected()
                 && display_on_settings_screen())
        {
            long_done = true;
            menu_open();
        }
    }
    else if (!level && pressed)
    {
        pressed = false;

        if (long_done || wake_only)
            return;

        if (menu_is_open())
        {
            menu_move(+1);
        }
        else if (connected_at_press)
        {
            if (timestamp - press_time < UI_LONG_PRESS_US)
                display_next_screen();
        }
        else if (!_any_connected())
        {
            // Mode changes reboot the adapter, so never do one while a
            // controller is in use
            adapter_mode_cycle(true);
        }
    }
}

// While the menu is open the controller drives it (the game sees the
// controller at rest meanwhile): D-pad up/down picks an item, left/right
// or A changes it, B saves and closes. A on "Save & exit" also closes.
static void _ui_menu_task(uint32_t timestamp)
{
    static interval_s sample_state = {0};
    static uint32_t last_buttons = 0;

    if (!interval_run(timestamp, UI_SAMPLE_US, &sample_state))
        return;

    if (!menu_is_open())
    {
        last_buttons = 0xFFFFFFFFu;  // ignore whatever is held on opening
        return;
    }

    joybus_input_s in;
    if (!joybus_itf_get_menu_input(&in)
        || (timestamp - menu_last_input() >= MENU_TIMEOUT_US))
    {
        menu_close();
        return;
    }

    // D-pad and button bits of the reply
    uint32_t buttons = in.byte_1 & 0xFFFF0000u;
    uint32_t edges = buttons & ~last_buttons;
    last_buttons = buttons;

    joybus_input_s e = {.byte_1 = edges};
    if (e.dpad_up)
        menu_move(-1);
    if (e.dpad_down)
        menu_move(+1);
    if (e.dpad_left)
        menu_change(-1);
    if (e.dpad_right || e.button_a)
        menu_change(+1);
    if (e.button_b)
        menu_close();

    if (edges)
        display_wake();
}

// Settings location; must match FLASH_TARGET_OFFSET in
// common/ll/adapter_ll_rp2040.c, which loads them at boot
#define UI_SETTINGS_FLASH_OFFSET ((1200 * 1024) + FLASH_SECTOR_SIZE)

// The common adapter_ll_save_check() builds this page on the stack. Core 0
// has a 2 KB stack, so that 4 KB buffer runs into core 1's stack just below
// it and crashes the OLED task. A static buffer keeps it in normal RAM.
static uint8_t _ui_save_page[FLASH_SECTOR_SIZE];

// Erase and write one flash sector. Core 1 runs from flash too, so it has
// to be parked in RAM while the flash is busy.
static void _ui_flash_write(uint32_t offset, const uint8_t *page)
{
    bool pause_core1 = display_running();
    if (pause_core1)
        multicore_lockout_start_blocking();

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, page, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    if (pause_core1)
        multicore_lockout_end_blocking();
}

// Write pending settings to flash: the common adapter settings, then the
// menu settings
static void _ui_save_check()
{
    if (_save_flag)
    {
        static_assert(sizeof(adapter_settings_s) <= FLASH_SECTOR_SIZE);
        memset(_ui_save_page, 0, sizeof(_ui_save_page));
        memcpy(_ui_save_page, _mem_settings_ptr, sizeof(adapter_settings_s));

        _ui_flash_write(UI_SETTINGS_FLASH_OFFSET, _ui_save_page);

        webusb_save_confirm();
        _save_flag = false;
    }

    uint32_t offset;
    if (user_settings_take_save(_ui_save_page, &offset))
        _ui_flash_write(offset, _ui_save_page);
}

void ui_main_loop()
{
    for (;;)
    {
        uint32_t t = adapter_ll_get_timestamp_us();

        _ui_save_check();

        rgb_task(t);
        adapter_comms_task(t);
        _ui_button_task(t);
        _ui_menu_task(t);
    }
}
