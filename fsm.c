#include <stdint.h>
#include <string.h>
#include "pico/time.h"
#include "led_clock.h"

extern fsm_t fsm;
extern time_encoded_t current_time;
extern bool time_synced;
extern uint8_t network_status;
extern uint64_t last_display_time;

const char* state_names[] = {"IDLE", "BLINK_HOUR", "TRANSITION_1", "BLINK_QUARTER", "TRANSITION_2", "BLINK_MINUTE"};

void fsm_init(fsm_t *fsm) {
    fsm->current_state = STATE_IDLE;
    fsm->blink_count = 0;
    fsm->blink_total = 0;
    fsm->led_on = false;
    fsm->last_tick = 0;
    fsm->transition_active = false;
    fsm->transition_start = 0;
}

void start_display(void) {
    current_time = encode_time();
    fsm.blink_count = 0;
    fsm.blink_total = current_time.n_hour;
    fsm.led_on = false;
    fsm.last_tick = to_ms_since_boot(get_absolute_time());
    fsm.current_state = STATE_BLINK_HOUR;
}

void fsm_update(fsm_t *fsm) {
    static uint32_t last_log_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    
    if (!time_synced) {
        if (now - last_log_time >= 1000) {
            debug_print("fsm_update: Time not synced, network_status=%d\n", network_status);
            last_log_time = now;
        }
        if (now % 1000 < 200) {
            switch (network_status) {
                case 1: set_ws2812(0, 0, 255); break;
                case 2: set_ws2812(255, 0, 255); break;
                case 3: set_ws2812(0, 255, 0); break;
                default: set_ws2812(255, 0, 0); break;
            }
        } else {
            all_leds_off();
        }
        return;
    }
    
    switch (fsm->current_state) {
        case STATE_IDLE:
            if (now_us - last_display_time >= 3000000) {
                debug_print("fsm_update: Starting display after 3 seconds idle\n");
                start_display();
                last_display_time = now_us;
            }
            break;
            
        case STATE_BLINK_HOUR:
            if (now - fsm->last_tick >= 500) {
                fsm->last_tick = now;
                fsm->led_on = !fsm->led_on;
                
                if (fsm->led_on) {
                    fsm->blink_count++;
                    set_ws2812(0, 255, 0);  // 小时：红色 (GRB格式: R在第二位)
                    debug_print("fsm_update: BLINK_HOUR %d/%d\n", fsm->blink_count, fsm->blink_total);
                } else {
                    all_leds_off();
                    if (fsm->blink_count >= fsm->blink_total) {
                        fsm->current_state = STATE_TRANSITION_1;
                        fsm->transition_start = now;
                        debug_print("fsm_update: Transition to TRANSITION_1\n");
                    }
                }
            }
            break;
            
        case STATE_TRANSITION_1:
            if (now - fsm->transition_start >= 1000) {
                current_time = encode_time();
                fsm->blink_count = 0;
                fsm->blink_total = current_time.n_quarter;
                fsm->led_on = false;
                fsm->last_tick = now;
                
                // 0刻钟时跳过绿灯闪烁
                if (fsm->blink_total == 0) {
                    fsm->current_state = STATE_TRANSITION_2;
                    fsm->transition_start = now;
                    debug_print("fsm_update: Skip BLINK_QUARTER (0 quarter)\n");
                } else {
                    fsm->current_state = STATE_BLINK_QUARTER;
                    debug_print("fsm_update: Transition to BLINK_QUARTER, quarter=%d\n", fsm->blink_total);
                }
            } else {
                all_leds_off();
            }
            break;
            
        case STATE_BLINK_QUARTER:
            if (now - fsm->last_tick >= 500) {
                fsm->last_tick = now;
                fsm->led_on = !fsm->led_on;
                
                if (fsm->led_on) {
                    fsm->blink_count++;
                    set_ws2812(255, 0, 0);  // 刻钟：绿色 (GRB格式: G在第一位)
                    debug_print("fsm_update: BLINK_QUARTER %d/%d\n", fsm->blink_count, fsm->blink_total);
                } else {
                    all_leds_off();
                    if (fsm->blink_count >= fsm->blink_total) {
                        fsm->current_state = STATE_TRANSITION_2;
                        fsm->transition_start = now;
                        debug_print("fsm_update: Transition to TRANSITION_2\n");
                    }
                }
            }
            break;
            
        case STATE_TRANSITION_2:
            if (now - fsm->transition_start >= 1000) {
                current_time = encode_time();
                fsm->blink_count = 0;
                fsm->blink_total = current_time.n_minute_rem;
                fsm->led_on = false;
                fsm->last_tick = now;
                
                // 0分钟余数时跳过蓝灯闪烁，直接回到IDLE
                if (fsm->blink_total == 0) {
                    fsm->current_state = STATE_IDLE;
                    last_display_time = to_us_since_boot(get_absolute_time());
                    debug_print("fsm_update: Skip BLINK_MINUTE (0 minute rem), back to IDLE\n");
                } else {
                    fsm->current_state = STATE_BLINK_MINUTE;
                    debug_print("fsm_update: Transition to BLINK_MINUTE, minute=%d\n", fsm->blink_total);
                }
            } else {
                all_leds_off();
            }
            break;
            
        case STATE_BLINK_MINUTE:
            if (now - fsm->last_tick >= 500) {
                fsm->last_tick = now;
                fsm->led_on = !fsm->led_on;
                
                if (fsm->led_on) {
                    fsm->blink_count++;
                    set_ws2812(0, 0, 255);
                    debug_print("fsm_update: BLINK_MINUTE %d/%d\n", fsm->blink_count, fsm->blink_total);
                } else {
                    all_leds_off();
                    if (fsm->blink_count >= fsm->blink_total) {
                        fsm->current_state = STATE_IDLE;
                        debug_print("fsm_update: Transition to IDLE\n");
                    }
                }
            }
            break;
    }
}