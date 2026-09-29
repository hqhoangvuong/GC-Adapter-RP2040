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

#if (ADAPTER_GC_PIN_BASE < 0) || (ADAPTER_GC_PIN_BASE > 26)
    #error "ADAPTER_GC_PIN_BASE must be 0-26 (ports use BASE..BASE+3)"
#endif
#if (UTIL_RGB_PIN >= ADAPTER_GC_PIN_BASE) && (UTIL_RGB_PIN <= ADAPTER_GC_PIN_BASE + 3)
    #error "GC data pins overlap the RGB LED pin"
#endif
#if (ADAPTER_BUTTON_1 >= ADAPTER_GC_PIN_BASE) && (ADAPTER_BUTTON_1 <= ADAPTER_GC_PIN_BASE + 3)
    #error "GC data pins overlap ADAPTER_BUTTON_1"
#endif
#if (ADAPTER_BUTTON_2 >= ADAPTER_GC_PIN_BASE) && (ADAPTER_BUTTON_2 <= ADAPTER_GC_PIN_BASE + 3)
    #error "GC data pins overlap ADAPTER_BUTTON_2"
#endif

#define UTIL_RGB_COUNT 4
#define UTIL_RGBW_EN 0

#define JOYBUS_PIO pio0

// The joybus PIO program drives four consecutive pins
#define JOYBUS_PORT_1 (ADAPTER_GC_PIN_BASE)
#define JOYBUS_PORT_2 (ADAPTER_GC_PIN_BASE + 1)
#define JOYBUS_PORT_3 (ADAPTER_GC_PIN_BASE + 2)
#define JOYBUS_PORT_4 (ADAPTER_GC_PIN_BASE + 3)
