#include "main.h"

#if defined(ADAPTER_BOARD_RP2040_ZERO)
#include "display.h"
#include "ui_button.h"
#include "user_settings.h"
#endif

bool cb_adapter_hardware_test()
{
    adapter_ll_hardware_setup();
    rgb_init();

    // Check GPIO levels if they are all HIGH indicating pull-ups are working
    bool gpio_fail = false;
    for(uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        if(!adapter_ll_gpio_read(JOYBUS_PORT_1 + i))
        {
            #if defined(ADAPTER_BOARD_RP2040_ZERO)
            if(!gpio_fail) display_set_fault_pullup(JOYBUS_PORT_1 + i);
            #endif
            gpio_fail=true;
        }
    }

    // If the test has failed, show orange now: the caller halts without
    // ever pushing its own colour to the LEDs
    if(gpio_fail)
    {
        rgb_set_instant(COLOR_ORANGE.color);
        return false;
    }

    // Flash all three rgb colors then we can return our test value
    rgb_set_instant(COLOR_RED.color);
    sleep_ms(1000);
    rgb_set_instant(COLOR_GREEN.color);
    sleep_ms(1000);
    rgb_set_instant(COLOR_BLUE.color);
    sleep_ms(1000);
    rgb_set_instant(COLOR_WHITE.color);
    sleep_ms(1000);
    rgb_set_instant(0x00);
    sleep_ms(1000);
    return true;
}

int main()
{
#if defined(ADAPTER_BOARD_RP2040_ZERO)
    // Menu settings first: the OLED reads brightness and rotation from them
    user_settings_load();
    // The OLED task starts first so it can report a failed self-test
    display_start();
    adapter_main_init();
    display_set_ready();
    ui_main_loop();
#else
    adapter_main_init();
    adapter_main_loop();
#endif
}
