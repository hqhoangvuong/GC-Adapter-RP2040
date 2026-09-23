#include "adapter_includes.h"
#include "ws2812.pio.h"
#include "joybus.pio.h"

#define RGB_PIO pio1
#define RGB_SM 0

#define JOYBUS_PIO pio0

#if defined(BOARD_RP2040_ZERO)

#define UTIL_RGB_PIN   16
#define UTIL_RGB_COUNT 1
#define UTIL_RGBW_EN 0
// Set to 1 if the onboard LED shows red and green swapped
#define UTIL_RGB_SWAP_RG 0

#define JOYBUS_PORT_1 0

#else

#define UTIL_RGB_PIN   10
#define UTIL_RGB_COUNT 4
#define UTIL_RGBW_EN 0
#define UTIL_RGB_SWAP_RG 0

#define JOYBUS_PORT_1 22
#define JOYBUS_PORT_2 23
#define JOYBUS_PORT_3 24
#define JOYBUS_PORT_4 25

#endif
