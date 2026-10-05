#ifndef DISPLAY_H
#define DISPLAY_H

#include "adapter_includes.h"

#if defined(ADAPTER_OLED)

// Start the OLED task on core 1. Safe with no display fitted.
void display_start();

// True once core 1 runs. Flash writes must then pause it first.
bool display_running();

// Core 0 finished start-up; show the normal screens from now on
void display_set_ready();

// Show the next screen (only while a controller is connected)
void display_next_screen();

// True while the stick range test screen is selected
bool display_on_range_screen();

// Briefly confirm that the current mode was saved as the default
void display_show_saved(input_mode_t mode);

// Note user activity: the idle screen dims after a minute and switches off
// after ten. Returns true if the screen was dimmed or off, so the caller
// can treat that button press as "wake up" only.
bool display_wake();

// The start-up self-test found no pull-up on this GPIO
void display_set_fault_pullup(uint pin);

#else

static inline void display_start() {}
static inline bool display_running() { return false; }
static inline void display_set_ready() {}
static inline void display_next_screen() {}
static inline bool display_on_range_screen() { return false; }
static inline void display_show_saved(input_mode_t mode) { (void)mode; }
static inline bool display_wake() { return false; }
static inline void display_set_fault_pullup(uint pin) { (void)pin; }

#endif

#endif
