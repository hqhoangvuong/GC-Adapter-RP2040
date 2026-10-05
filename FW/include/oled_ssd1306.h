#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <stdint.h>
#include <stdbool.h>

#define OLED_WIDTH  128
#define OLED_HEIGHT 32
// One byte holds a column of 8 pixels (LSB on top); a page is 8 rows
#define OLED_FB_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)

// Set up the I2C pins. Call once before anything else.
void oled_bus_init();

// Look for the display (0x3C, then 0x3D) and send the init sequence.
// Returns false if nothing answers.
bool oled_init();

// Send a whole frame. Returns false if the display stopped answering.
bool oled_present(const uint8_t *fb);

// Panel brightness: contrast 0-255 when awake, or dimmed. Contrast alone
// changes an SSD1306 very little, so dimming also lowers the pixel
// precharge and drive voltage.
bool oled_set_brightness(uint8_t contrast, bool dim);

// Rotate the picture 180 degrees. Takes effect from the next frame.
bool oled_set_flip(bool flip);

// Turn the panel on or off. RAM contents are kept while it is off.
bool oled_set_on(bool on);

#endif
