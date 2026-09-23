#include "main.h"

void rgb_itf_update(rgb_s *leds)
{
    for(uint8_t i = 0; i < ADAPTER_RGB_COUNT; i++)
    {
        uint32_t color = leds[i].color;
#if (UTIL_RGB_SWAP_RG)
        // Swap the G (bits 31-24) and R (bits 23-16) bytes
        color = ((color & 0x00FF0000) << 8) | ((color >> 8) & 0x00FF0000) | (color & 0x0000FFFF);
#endif
        pio_sm_put_blocking(RGB_PIO, RGB_SM, color);
    }
}

void rgb_itf_init()
{
    uint offset = pio_add_program(RGB_PIO, &ws2812_program);
    ws2812_program_init(RGB_PIO, RGB_SM, offset, UTIL_RGB_PIN, UTIL_RGBW_EN);
}