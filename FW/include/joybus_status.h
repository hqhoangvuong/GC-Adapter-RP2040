#ifndef JOYBUS_STATUS_H
#define JOYBUS_STATUS_H

#include "adapter_includes.h"

typedef enum
{
    JOYBUS_EVENT_NONE,
    JOYBUS_EVENT_CONNECT,       // controller answered the probe and origin
    JOYBUS_EVENT_DROP,          // 10 missed reads in a row
    JOYBUS_EVENT_ORIGIN_ASK,    // reply had the get-origin flag set
    JOYBUS_EVENT_ORIGIN_READ,   // origin re-read while connected
    JOYBUS_EVENT_ORIGIN_FAIL,   // no reply to that origin re-read
    JOYBUS_EVENT_RECENTER,      // X+Y+Start took a new centre
} joybus_event_t;

#define JOYBUS_EVENT_COUNT 3

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
    // Times X+Y+Start reset the stick centre, since boot
    uint32_t recenters;

    // The controller dropped during an X+Y+Start hold (it resets itself)
    // and hasn't reconnected yet; recenter_drop_time is when it dropped
    bool recenter_pending;
    uint32_t recenter_drop_time;

    // Last few events, newest first, for the OLED event screen
    uint8_t event_type[JOYBUS_EVENT_COUNT];
    uint32_t event_time[JOYBUS_EVENT_COUNT];
} joybus_port_status_s;

void joybus_itf_get_status(uint port, joybus_port_status_s *out);

#endif
