#ifndef UI_BUTTON_H
#define UI_BUTTON_H

// Main loop for boards with one button (RP2040-Zero). Replaces
// adapter_main_loop(), whose mode task expects the GC Pocket+ buttons.
void ui_main_loop();

#endif
