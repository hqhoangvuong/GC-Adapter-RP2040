#include "main.h"

void rgb_itf_update(rgb_s *leds)
{
    for(uint8_t i = 0; i < ADAPTER_RGB_COUNT; i++)
    {
        rgb_s out = leds[i];
        #if defined(ADAPTER_LED_RGB_ORDER)
        // LED expects red first: swap the red and green bytes
        out.r = leds[i].g;
        out.g = leds[i].r;
        #endif
        pio_sm_put_blocking(RGB_PIO, RGB_SM, out.color);
    }
}

void rgb_itf_init()
{
    uint offset = pio_add_program(RGB_PIO, &ws2812_program);
    ws2812_program_init(RGB_PIO, RGB_SM, offset, UTIL_RGB_PIN, UTIL_RGBW_EN);
}