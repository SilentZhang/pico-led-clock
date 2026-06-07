#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "led_clock.h"

#define GPIO_OVERRIDE_LOW 2
#define GPIO_OVERRIDE_NORMAL 0

extern uint32_t current_hour;
extern uint32_t current_minute;
extern uint32_t current_second;
extern uint64_t last_time_update;
extern bool time_synced;

typedef enum {
    BTN_WAIT_FIRST,
    BTN_WAIT_SECOND,
    BTN_WAIT_THIRD,
    BTN_DONE
} btn_set_state_t;

static btn_set_state_t btn_set_state = BTN_WAIT_FIRST;
static uint32_t btn_count_hour = 0;
static uint32_t btn_count_quarter = 0;
static uint32_t btn_count_minute = 0;
static uint64_t btn_last_press_time = 0;
static bool btn_was_pressed = false;
static uint32_t btn_this_press_count = 0;

bool __no_inline_not_in_flash_func(read_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;
    
    uint32_t flags = save_and_disable_interrupts();
    
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    for (volatile int i = 0; i < 1000; ++i);
    
    #if PICO_RP2040
    #define CS_BIT (1u << 1)
    #else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
    #endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);
    
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    restore_interrupts(flags);
    return button_state;
}

void offline_time_set_init(void) {
    btn_set_state = BTN_WAIT_FIRST;
    btn_count_hour = 0;
    btn_count_quarter = 0;
    btn_count_minute = 0;
    btn_last_press_time = 0;
    btn_was_pressed = false;
    btn_this_press_count = 0;
}

void offline_time_set_update(void) {
    uint64_t now = to_us_since_boot(get_absolute_time());
    bool btn_pressed = read_bootsel_button();
    
    static uint64_t last_btn_debug = 0;
    if (now - last_btn_debug >= 1000000) {
        debug_print("BOOTSEL: pressed=%d\n", btn_pressed);
        last_btn_debug = now;
    }
    
    switch (btn_set_state) {
        case BTN_WAIT_FIRST:
            if (btn_pressed && !btn_was_pressed) {
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Hour press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                uint32_t idle_time = now - btn_last_press_time;
                
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                if (idle_time > 1000000) {
                    btn_count_hour = btn_this_press_count;
                    if (btn_count_hour > 12) btn_count_hour = 12;
                    btn_set_state = BTN_WAIT_SECOND;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                    debug_print("offline: Hour=%d, waiting for quarter (1-5s)\n", btn_count_hour);
                }
            }
            break;
            
        case BTN_WAIT_SECOND:
            if (!btn_pressed && !btn_was_pressed && btn_this_press_count == 0) {
                uint32_t idle_time = now - btn_last_press_time;
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
            }
            
            if (btn_pressed && !btn_was_pressed) {
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Quarter press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                uint32_t idle_time = now - btn_last_press_time;
                
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                if (idle_time > 1000000) {
                    if (btn_this_press_count > 3) {
                        btn_count_quarter = 0;
                    } else {
                        btn_count_quarter = btn_this_press_count;
                    }
                    btn_set_state = BTN_WAIT_THIRD;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                    debug_print("offline: Quarter=%d, waiting for minute (1-5s)\n", btn_count_quarter);
                }
            }
            break;
            
        case BTN_WAIT_THIRD:
            if (!btn_pressed && !btn_was_pressed && btn_this_press_count == 0) {
                uint32_t idle_time = now - btn_last_press_time;
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
            }
            
            if (btn_pressed && !btn_was_pressed) {
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Minute press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                uint32_t idle_time = now - btn_last_press_time;
                
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                if (idle_time > 1000000) {
                    if (btn_this_press_count > 15) {
                        btn_count_minute = 0;
                    } else {
                        btn_count_minute = btn_this_press_count;
                    }
                    
                    current_hour = btn_count_hour;
                    current_minute = btn_count_quarter * 15 + btn_count_minute;
                    if (current_minute >= 60) current_minute = 59;
                    current_second = 0;
                    last_time_update = now;
                    time_synced = true;
                    
                    debug_print("offline: Time set to %02d:%02d:%02d\n", current_hour, current_minute, current_second);
                    
                    btn_set_state = BTN_DONE;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                }
            }
            break;
            
        case BTN_DONE:
            if (now - btn_last_press_time > 5000000) {
                offline_time_set_init();
                debug_print("offline: Ready for new setting\n");
            }
            break;
    }
    
    btn_was_pressed = btn_pressed;
}