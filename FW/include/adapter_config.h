#if defined(ADAPTER_BOARD_RP2040_ZERO)
    // One button does everything (see src/ui_button.c). It also enters
    // BOOTSEL when held while USB is plugged in.
    #ifndef ADAPTER_BUTTON_PIN
        #define ADAPTER_BUTTON_PIN 12
    #endif
    #define ADAPTER_BUTTON_1  ADAPTER_BUTTON_PIN
    #define ADAPTER_BUTTON_2  -1
#else
    #define ADAPTER_BUTTON_1  11
    #define ADAPTER_BUTTON_2  12
#endif
#define ADAPTER_RGB_COUNT 4
// Number of GC ports (1-4). CMake can override it for hand-wired boards.
#ifndef ADAPTER_PORT_COUNT
#define ADAPTER_PORT_COUNT 4
#endif

#define ADAPTER_MANUFACTURER "HHL"
#define ADAPTER_PRODUCT "GC Pocket+"
#define ADAPTER_STRING "GCP+"

#define ADAPTER_FIRMWARE_VERSION 0x000B
#define ADAPTER_SETTINGS_VERSION 0x0003
#define ADAPTER_WEBUSB_URL "handheldlegend.github.io/gcp"