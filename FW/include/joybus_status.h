#ifndef JOYBUS_STATUS_H
#define JOYBUS_STATUS_H

#include "adapter_includes.h"

// A consistent copy of one port's state, for code that runs on core 1
// (the OLED). Core 0 publishes it after every poll.
typedef struct
{
    // Inputs with the origin offsets applied. port_itf > -1 means connected.
    joybus_input_s input;

    // Raw values the controller reported at rest when it connected
    uint8_t origin_lx;
    uint8_t origin_ly;
    uint8_t origin_rx;
    uint8_t origin_ry;
    uint8_t origin_lt;
    uint8_t origin_rt;

    bool rumble;

    // Good reads since boot, for measuring the poll rate
    uint32_t reads;
    // Missed reads since this controller connected
    uint32_t misses;
    // Times a connected controller stopped answering, since boot
    uint32_t drops;
    // time_us_32() when this controller connected
    uint32_t connect_time;
} joybus_port_status_s;

void joybus_itf_get_status(uint port, joybus_port_status_s *out);

#endif
