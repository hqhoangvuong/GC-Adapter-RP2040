#if defined(BOARD_RP2040_ZERO)
// Waveshare RP2040-Zero: one mode button (short press cycles mode,
// 5s hold saves it as default), onboard LED, 1 controller port
#define ADAPTER_BUTTON_1  14
#define ADAPTER_BUTTON_2  -1
#define ADAPTER_RGB_COUNT 1
#define ADAPTER_PORT_COUNT 1
#else
#define ADAPTER_BUTTON_1  11
#define ADAPTER_BUTTON_2  12
#define ADAPTER_RGB_COUNT 4
#define ADAPTER_PORT_COUNT 4
#endif

#define ADAPTER_MANUFACTURER "HHL"
#define ADAPTER_PRODUCT "GC Pocket+"
#define ADAPTER_STRING "GCP+"

#define ADAPTER_FIRMWARE_VERSION 0x000B
#define ADAPTER_SETTINGS_VERSION 0x0003
#define ADAPTER_WEBUSB_URL "handheldlegend.github.io/gcp"
