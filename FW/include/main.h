#include "adapter_includes.h"
#include "ws2812.pio.h"
#include "joybus.pio.h"

#define RGB_PIO pio1
#define RGB_SM 0

#if defined(ADAPTER_BOARD_RP2040_ZERO)
    // Waveshare RP2040-Zero: one onboard WS2812 on GP16, GC data on
    // ADAPTER_GC_PIN_BASE..+3 (set from CMake, default GP26-GP29)
    #define UTIL_RGB_PIN 16
    #ifndef ADAPTER_GC_PIN_BASE
        #define ADAPTER_GC_PIN_BASE 26
    #endif
#else
    // GC Pocket+ PCB
    #define UTIL_RGB_PIN 10
    #define ADAPTER_GC_PIN_BASE 22
#endif

// Last GC data pin in use
#define ADAPTER_GC_PIN_LAST (ADAPTER_GC_PIN_BASE + ADAPTER_PORT_COUNT - 1)

#if (ADAPTER_PORT_COUNT < 1) || (ADAPTER_PORT_COUNT > 4)
    #error "ADAPTER_PORT_COUNT must be 1-4"
#endif
#if (ADAPTER_GC_PIN_BASE < 0) || (ADAPTER_GC_PIN_LAST > 29)
    #error "GC data pins must lie within GP0-GP29"
#endif
#if (UTIL_RGB_PIN >= ADAPTER_GC_PIN_BASE) && (UTIL_RGB_PIN <= ADAPTER_GC_PIN_LAST)
    #error "GC data pins overlap the RGB LED pin"
#endif
#if (ADAPTER_BUTTON_1 >= ADAPTER_GC_PIN_BASE) && (ADAPTER_BUTTON_1 <= ADAPTER_GC_PIN_LAST)
    #error "GC data pins overlap ADAPTER_BUTTON_1"
#endif
#if (ADAPTER_BUTTON_2 >= ADAPTER_GC_PIN_BASE) && (ADAPTER_BUTTON_2 <= ADAPTER_GC_PIN_LAST)
    #error "GC data pins overlap ADAPTER_BUTTON_2"
#endif
#if (ADAPTER_BUTTON_1 == UTIL_RGB_PIN)
    #error "The button pin is the RGB LED pin"
#endif

#if defined(ADAPTER_OLED)
    // Each I2C block owns pin pairs in turn: GP0/1 I2C0, GP2/3 I2C1, GP4/5
    // I2C0 and so on. SDA is the even pin of a pair, SCL the odd one.
    #if ((ADAPTER_OLED_SDA) % 2 != 0) || ((ADAPTER_OLED_SCL) != (ADAPTER_OLED_SDA) + 1) || ((ADAPTER_OLED_SCL) > 29)
        #error "OLED SDA must be an even GPIO and SCL the next one (e.g. GP4 and GP5)"
    #endif
    #define OLED_I2C ((((ADAPTER_OLED_SDA) / 2) % 2) ? i2c1 : i2c0)
    #if ((ADAPTER_OLED_SCL) >= ADAPTER_GC_PIN_BASE) && ((ADAPTER_OLED_SDA) <= ADAPTER_GC_PIN_LAST)
        #error "OLED pins overlap the GC data pins"
    #endif
    #if ((ADAPTER_OLED_SDA) == UTIL_RGB_PIN) || ((ADAPTER_OLED_SCL) == UTIL_RGB_PIN)
        #error "OLED pins overlap the RGB LED pin"
    #endif
    #if ((ADAPTER_OLED_SDA) == ADAPTER_BUTTON_1) || ((ADAPTER_OLED_SCL) == ADAPTER_BUTTON_1)
        #error "OLED pins overlap the button pin"
    #endif
#endif

#define UTIL_RGB_COUNT 4
#define UTIL_RGBW_EN 0

#define JOYBUS_PIO pio0

// The joybus PIO program drives ADAPTER_PORT_COUNT consecutive pins
#define JOYBUS_PORT_1 (ADAPTER_GC_PIN_BASE)
