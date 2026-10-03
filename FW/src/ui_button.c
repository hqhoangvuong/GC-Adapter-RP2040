#include "main.h"
#include "display.h"
#include "ui_button.h"
#include "hardware/sync.h"

// One button, wired from ADAPTER_BUTTON_PIN to GND:
//   No controller:        click = next mode (the adapter reboots into it)
//                         hold  = save the current mode as the default
//   Controller connected: click = next OLED screen
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
    }
    else if (level && pressed)
    {
        // Save as soon as the hold is long enough, so the screen confirms
        // it while the button is still down
        if (!long_done && !connected_at_press && !_any_connected()
            && (timestamp - press_time >= UI_LONG_PRESS_US))
        {
            long_done = true;
            settings_set_mode(adapter_get_current_mode());
            settings_save();
            display_show_saved(adapter_get_current_mode());
        }
    }
    else if (!level && pressed)
    {
        pressed = false;

        if (long_done)
            return;

        if (connected_at_press)
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

// Settings location; must match FLASH_TARGET_OFFSET in
// common/ll/adapter_ll_rp2040.c, which loads them at boot
#define UI_SETTINGS_FLASH_OFFSET ((1200 * 1024) + FLASH_SECTOR_SIZE)

// The common adapter_ll_save_check() builds this page on the stack. Core 0
// has a 2 KB stack, so that 4 KB buffer runs into core 1's stack just below
// it and crashes the OLED task. A static buffer keeps it in normal RAM.
static uint8_t _ui_save_page[FLASH_SECTOR_SIZE];

// Write pending settings to flash. Core 1 runs from flash too, so it has
// to be parked in RAM while the flash is busy.
static void _ui_save_check()
{
    if (!_save_flag)
        return;

    static_assert(sizeof(adapter_settings_s) <= FLASH_SECTOR_SIZE);
    memset(_ui_save_page, 0, sizeof(_ui_save_page));
    memcpy(_ui_save_page, _mem_settings_ptr, sizeof(adapter_settings_s));

    bool pause_core1 = display_running();
    if (pause_core1)
        multicore_lockout_start_blocking();

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(UI_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(UI_SETTINGS_FLASH_OFFSET, _ui_save_page, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    if (pause_core1)
        multicore_lockout_end_blocking();

    webusb_save_confirm();
    _save_flag = false;
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
    }
}
