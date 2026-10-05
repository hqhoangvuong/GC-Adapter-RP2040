#include "main.h"
#include "joybus_status.h"

#define CLAMP_0_255(value) ((value) < 0 ? 0 : ((value) > 255 ? 255 : (value)))

uint32_t _usb_interval = 7000;

uint _adapter_output_irq;
uint _adapter_input_irq;
uint _gamecube_offset;
pio_sm_config _gamecube_c[4];

const uint8_t _led_defs[4] = {0, 1, 2, 3};

volatile bool _gc_tx_done = false;
bool _gc_running = false;

#define ALIGNED_JOYBUS_8(val) ((val) << 24)

// One state machine per port
#define JOYBUS_SM_MASK ((1u << ADAPTER_PORT_COUNT) - 1)

uint8_t _port_phases[4] = {0};
uint32_t _port_probes[4] = {0};
uint32_t _port_inputs[4][4] = {{0}};
joybus_input_s _port_joybus[4] = {0, 0, 0, 0};

#define ORIGIN_DELAY_CYCLES 8
uint8_t delay_cycles = 0;

bool _port_rumble[4] = {false, false, false, false};

typedef struct
{
    int lx_offset;
    int ly_offset;
    int rx_offset;
    int ry_offset;
    int lt_offset;
    int rt_offset;
} analog_offset_s;

analog_offset_s _port_offsets[4] = {0};

// Published copy of each port for core 1. _status_seq is odd while core 0
// is writing, so a reader retries instead of using a half-written copy.
static joybus_port_status_s _port_status[4] = {0};
static volatile uint32_t _status_seq = 0;

uint read_count = 0;

// Holding X+Y+Start this long resets the stick centre, as on a GameCube
#define RECENTER_HOLD_US 3000000
// Least time between origin re-reads the controller asks for
#define REORIGIN_MIN_US  1000000

// Byte 0 bit 5 of a poll reply: the controller wants its origin read again
#define JOYBUS_GET_ORIGIN(byte_1) (((byte_1) >> 29) & 1)

// Short drop while X+Y+Start is held: an official controller resets itself
// at 3 s and may stop answering for a moment. A reconnect this soon after
// such a drop counts as the recentre, since connecting reads the origin.
#define RECENTER_RECONNECT_US 3000000

static uint32_t _port_reorigin_time[4] = {0};

// Consecutive missed reads
static uint8_t _port_miss_run[4] = {0};

// X+Y+Start hold tracking
static bool _combo_held[4] = {false};
static bool _combo_done[4] = {false};
static uint32_t _combo_start[4] = {0};

// Set when a port drops during a long X+Y+Start hold
static bool _drop_during_combo[4] = {false};
static uint32_t _drop_time[4] = {0};

static void _gc_port_event(uint port, joybus_event_t type)
{
    for (int i = JOYBUS_EVENT_COUNT - 1; i > 0; i--)
    {
        _port_status[port].event_type[i] = _port_status[port].event_type[i - 1];
        _port_status[port].event_time[i] = _port_status[port].event_time[i - 1];
    }
    _port_status[port].event_type[0] = type;
    _port_status[port].event_time[0] = time_us_32();
}

void _gc_port_reset(uint port)
{
    _port_joybus[port].port_itf = -1;
    _port_phases[port] = 0;
    // The host can't switch the motor off for a port that has gone, so a
    // controller plugged back in would otherwise start out rumbling
    _port_rumble[port] = false;
}

// Treat the raw values now in _port_joybus[port] as the controller's rest
// position: they become the stick centres and trigger zeros
static void _gc_port_set_origin(uint port)
{
    _port_offsets[port].lx_offset = 128 - (int)_port_joybus[port].stick_left_x;
    _port_offsets[port].rx_offset = 128 - (int)_port_joybus[port].stick_right_x;
    _port_offsets[port].ly_offset = 128 - (int)_port_joybus[port].stick_left_y;
    _port_offsets[port].ry_offset = 128 - (int)_port_joybus[port].stick_right_y;

    _port_offsets[port].lt_offset = -(int)_port_joybus[port].analog_trigger_l;
    _port_offsets[port].rt_offset = -(int)_port_joybus[port].analog_trigger_r;

    _port_status[port].origin_lx = _port_joybus[port].stick_left_x;
    _port_status[port].origin_ly = _port_joybus[port].stick_left_y;
    _port_status[port].origin_rx = _port_joybus[port].stick_right_x;
    _port_status[port].origin_ry = _port_joybus[port].stick_right_y;
    _port_status[port].origin_lt = _port_joybus[port].analog_trigger_l;
    _port_status[port].origin_rt = _port_joybus[port].analog_trigger_r;
}

static void _gc_port_recentered(uint port)
{
    _port_status[port].recenters += 1;
    _gc_port_event(port, JOYBUS_EVENT_RECENTER);
}

// X+Y+Start held for RECENTER_HOLD_US takes the current position as the new
// centre. Reads raw values, so call it before the offsets are applied.
static void _gc_port_check_recenter(uint port, uint32_t now)
{
    bool combo = _port_joybus[port].button_x && _port_joybus[port].button_y
                 && _port_joybus[port].button_start;

    if (!combo)
    {
        _combo_held[port] = false;
    }
    else if (!_combo_held[port])
    {
        _combo_held[port] = true;
        _combo_done[port] = false;
        _combo_start[port] = now;
    }
    else if (!_combo_done[port] && (now - _combo_start[port] >= RECENTER_HOLD_US))
    {
        _combo_done[port] = true;
        _gc_port_set_origin(port);
        _gc_port_recentered(port);
    }
}

// Count a missed read; 10 in a row means the controller has gone
static void _gc_port_miss(uint port)
{
    _port_miss_run[port] += 1;
    _port_status[port].misses += 1;

    if (_port_miss_run[port] >= 10)
    {
        uint32_t now = time_us_32();
        _drop_during_combo[port] = _combo_held[port] && !_combo_done[port]
                                   && (now - _combo_start[port] >= RECENTER_HOLD_US - 500000);
        _drop_time[port] = now;
        _combo_held[port] = false;

        _port_status[port].drops += 1;
        _gc_port_event(port, JOYBUS_EVENT_DROP);
        _gc_port_reset(port);
        _port_miss_run[port] = 0;
    }
}

void _gc_port_data(uint port)
{
    if (!_port_phases[port])
    {
        // For this specific circumstance, we must push
        // manually since our data is set to push auto 32 bits
        pio_sm_exec(JOYBUS_PIO, port, pio_encode_push(false, false));
        _port_probes[port] = pio_sm_get(JOYBUS_PIO, port) >> 17;

        if (_port_probes[port] & 0x09 )
        {
            // Successfully obtained GC info
            // Set our delay cycles so controllers have
            // a moment to adjust their voltage and settle
            // before getting calibration data
            delay_cycles = ORIGIN_DELAY_CYCLES;
            _port_phases[port] = 1;
        }

        _port_probes[port] = 0;
    }
    else if (_port_phases[port] == 1)
    {

        // Collect data for analog offset creation
        for (uint i = 0; i < 2; i++)
        {
            if (!pio_sm_is_rx_fifo_empty(JOYBUS_PIO, port))
            {
                _port_inputs[port][i] = pio_sm_get(JOYBUS_PIO, port);
            }
            else if (_port_joybus[port].port_itf > -1)
            {
                // An origin re-read went unanswered. Keep the controller
                // and its old centre; it can ask again.
                _gc_port_event(port, JOYBUS_EVENT_ORIGIN_FAIL);
                _port_phases[port] = 2;
                _gc_port_miss(port);
                return;
            }
            else
            {
                _gc_port_reset(port);
                return;
            }
        }

        _port_miss_run[port] = 0;
        _port_joybus[port].byte_1 = _port_inputs[port][0];
        _port_joybus[port].byte_2 = _port_inputs[port][1];

        _gc_port_set_origin(port);
        _port_reorigin_time[port] = time_us_32();

        // This reply is the rest position itself, so report it as centred
        _port_joybus[port].stick_left_x = 128;
        _port_joybus[port].stick_left_y = 128;
        _port_joybus[port].stick_right_x = 128;
        _port_joybus[port].stick_right_y = 128;
        _port_joybus[port].analog_trigger_l = 0;
        _port_joybus[port].analog_trigger_r = 0;

        // Set the port phase
        _port_phases[port] = 2;

        // A controller that asked for its origin again is already connected
        // and keeps its USB interface
        if (_port_joybus[port].port_itf > -1)
        {
            _gc_port_event(port, JOYBUS_EVENT_ORIGIN_READ);
            return;
        }

        // Set the port USB Interface
        int tmp_itf = 0;

        for (uint8_t i = 0; i < 4; i++)
        {
            bool itfInUse = false;

            for (uint8_t j = 0; j < 4; j++)
            {
                if (_port_joybus[j].port_itf == i)
                {
                    itfInUse = true;
                    break;
                }
            }

            if (!itfInUse)
            {
                tmp_itf = i;
                break; // Exit the loop once an available port number is found
            }
        }

        _port_joybus[port].port_itf = tmp_itf;

        _port_status[port].misses = 0;
        _port_status[port].connect_time = time_us_32();
        _gc_port_event(port, JOYBUS_EVENT_CONNECT);

        if (_drop_during_combo[port])
        {
            _drop_during_combo[port] = false;
            if (_port_status[port].connect_time - _drop_time[port] < RECENTER_RECONNECT_US)
                _gc_port_recentered(port);
        }
    }
    else if (_port_phases[port] == 2)
    {
        for (uint i = 0; i < 2; i++)
        {
            if (!pio_sm_is_rx_fifo_empty(JOYBUS_PIO, port))
            {
                _port_inputs[port][i] = pio_sm_get(JOYBUS_PIO, port);
            }
            else
            {   
                _gc_port_miss(port);
                return;
            }
        }

        // A good reply ends any run of misses, so only 10 misses
        // in a row count as an unplug
        _port_miss_run[port] = 0;
        _port_status[port].reads += 1;

        _port_joybus[port].byte_1 = _port_inputs[port][0];
        _port_joybus[port].byte_2 = _port_inputs[port][1];

        uint32_t now = time_us_32();
        _gc_port_check_recenter(port, now);

        // The controller sets this after its own X+Y+Start reset (and at
        // power-up): read its origin on the next poll. Rate-limited in case
        // a controller leaves the flag set.
        if (JOYBUS_GET_ORIGIN(_port_joybus[port].byte_1)
            && (now - _port_reorigin_time[port] >= REORIGIN_MIN_US))
        {
            _port_reorigin_time[port] = now;
            _port_phases[port] = 1;
            _gc_port_event(port, JOYBUS_EVENT_ORIGIN_ASK);
        }

        int lx = CLAMP_0_255(_port_joybus[port].stick_left_x + _port_offsets[port].lx_offset);
        int ly = CLAMP_0_255(_port_joybus[port].stick_left_y + _port_offsets[port].ly_offset);
        int rx = CLAMP_0_255(_port_joybus[port].stick_right_x + _port_offsets[port].rx_offset);
        int ry = CLAMP_0_255(_port_joybus[port].stick_right_y + _port_offsets[port].ry_offset);

        int lt = CLAMP_0_255(_port_joybus[port].analog_trigger_l + _port_offsets[port].lt_offset);
        int rt = CLAMP_0_255(_port_joybus[port].analog_trigger_r + _port_offsets[port].rt_offset);

        // Apply offsets
        _port_joybus[port].stick_left_x = (uint8_t)lx;
        _port_joybus[port].stick_left_y = (uint8_t)ly;
        _port_joybus[port].stick_right_x = (uint8_t)rx;
        _port_joybus[port].stick_right_y = (uint8_t)ry;

        _port_joybus[port].analog_trigger_l = (uint8_t)lt;
        _port_joybus[port].analog_trigger_r = (uint8_t)rt;
    }
}

void _gamecube_publish_status()
{
    _status_seq++;
    __dmb();
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        _port_status[i].input = _port_joybus[i];
        _port_status[i].rumble = _port_rumble[i];
    }
    __dmb();
    _status_seq++;
}

void joybus_itf_get_status(uint port, joybus_port_status_s *out)
{
    if (port >= ADAPTER_PORT_COUNT)
    {
        memset(out, 0, sizeof(*out));
        out->input.port_itf = -1;
        return;
    }

    uint32_t seq;
    do
    {
        seq = _status_seq;
        __dmb();
        *out = _port_status[port];
        __dmb();
    } while ((seq & 1) || (seq != _status_seq));
}

void _gamecube_get_data()
{
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        _gc_port_data(i);
    }
    _gamecube_publish_status();
}

void _gamecube_send_probe()
{
    pio_sm_clear_fifos(JOYBUS_PIO, 0);
    pio_set_sm_mask_enabled(JOYBUS_PIO, JOYBUS_SM_MASK, false);
    for (uint i = 0; i < ADAPTER_PORT_COUNT; i++)
    {
        switch (_port_phases[i])
        {
        default:
        case 0:
        {
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_set(pio_y, 0));
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_jmp(_gamecube_offset));
            pio_sm_put_blocking(JOYBUS_PIO, i, ALIGNED_JOYBUS_8(0x00));
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_set(pio_y, 7));
        }
        break;

        case 1:
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_set(pio_y, 0));
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_jmp(_gamecube_offset));
            pio_sm_put_blocking(JOYBUS_PIO, i, ALIGNED_JOYBUS_8(0x41));
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_set(pio_y, 7));
            break;

        case 2:
        {
            pio_sm_exec_wait_blocking(JOYBUS_PIO, i, pio_encode_jmp(_gamecube_offset + joybus_offset_joybusout));
            pio_sm_put_blocking(JOYBUS_PIO, i, ALIGNED_JOYBUS_8(0x40));
            pio_sm_put_blocking(JOYBUS_PIO, i, ALIGNED_JOYBUS_8(0x03));
            pio_sm_put_blocking(JOYBUS_PIO, i, ALIGNED_JOYBUS_8(_port_rumble[i]));
        }
        break;
        }
    }
    pio_set_sm_mask_enabled(JOYBUS_PIO, JOYBUS_SM_MASK, true);
}

void joybus_itf_enable_rumble(uint8_t interface, bool enable)
{
    for (uint i = 0; i < 4; i++)
    {
        if (_port_joybus[i].port_itf == interface)
        {
            _port_rumble[i] = enable;
            break;
        }
    }
}

void joybus_itf_poll(joybus_input_s **out)
{

    *out = _port_joybus;

    if(delay_cycles>0)
    {
        delay_cycles--;
        return;
    }

    _gamecube_send_probe();
    sleep_us(500);
    _gamecube_get_data();
}

void joybus_itf_init()
{
    _gamecube_offset = pio_add_program(JOYBUS_PIO, &joybus_program);
    for (uint i = 0; i < 4; i++)
    {
        memset(&_port_joybus[i], 0, sizeof(joybus_input_s));
        _port_joybus[i].port_itf = -1;
        _port_status[i].input.port_itf = -1;
    }

    joybus_program_init(JOYBUS_PIO, _gamecube_offset + joybus_offset_joybusout, JOYBUS_PORT_1, ADAPTER_PORT_COUNT, _gamecube_c);
    sleep_ms(100);
}