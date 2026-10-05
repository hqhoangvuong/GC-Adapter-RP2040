#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include "adapter_includes.h"

// Settings from the OLED menu (RP2040-Zero build). They live in their own
// flash sector, apart from the common adapter settings.

typedef enum { TRIGGERS_ANALOG, TRIGGERS_DIGITAL, TRIGGERS_COUNT } trigger_mode_t;
typedef enum { DEADZONE_OFF, DEADZONE_5, DEADZONE_10, DEADZONE_15, DEADZONE_COUNT } deadzone_t;
typedef enum { BRIGHTNESS_LOW, BRIGHTNESS_MEDIUM, BRIGHTNESS_HIGH, BRIGHTNESS_LEVELS } brightness_level_t;
typedef enum { SLEEP_1MIN, SLEEP_5MIN, SLEEP_NEVER, SLEEP_COUNT } sleep_mode_t;

typedef struct
{
    uint8_t triggers;    // trigger_mode_t
    uint8_t deadzone;    // deadzone_t
    uint8_t swap_abxy;   // swap A with B and X with Y
    uint8_t brightness;  // brightness_level_t
    uint8_t flip;        // rotate the OLED picture 180 degrees
    uint8_t sleep;       // sleep_mode_t
} user_settings_s;

// Read by both cores; written only by core 0
extern volatile user_settings_s g_user_settings;

// Load from flash, or set the defaults if nothing valid is stored
void user_settings_load();

// Ask for the current settings to be written to flash. The main loop does
// the write (see ui_button.c).
void user_settings_request_save();

// If a save is pending, copy the sector image to write into page and
// return true. offset gets the flash offset of the sector.
bool user_settings_take_save(uint8_t *page, uint32_t *offset);

// Apply trigger mode, deadzone and button swap to centred inputs
void user_settings_apply_input(joybus_input_s *in);

#endif
