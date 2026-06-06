#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tusb.h"
#include "led_clock.h"
#include "ws2812.pio.h"

static fsm_t fsm;
static time_encoded_t current_time;
static PIO ws2812_pio = pio0;
static uint ws2812_sm;

uint32_t current_hour = 7;
uint32_t current_minute = 37;
uint32_t current_second = 0;
static uint64_t last_time_update = 0;
static uint64_t last_display_time = 0;
bool time_synced = false;
static uint8_t network_status = 0; // 0: no link, 1: link up, 2: dhcp ok, 3: ntp ok

void debug_print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        for (int i = 0; i < len; i += 64) {
            int chunk = (len - i > 64) ? 64 : (len - i);
            tud_cdc_write(buffer + i, chunk);
            tud_task();
        }
        tud_cdc_write_flush();
    }
}

void indicate_network_status(uint8_t status) {
    debug_print("Network status changed: %d -> %d\n", network_status, status);
    network_status = status;
}

void set_ntp_callback(uint32_t hour, uint32_t minute, uint32_t second) {
    current_hour = hour;
    current_minute = minute;
    current_second = second;
    last_time_update = to_us_since_boot(get_absolute_time());
    time_synced = true;
    debug_print("Time synced: %02d:%02d:%02d\n", hour, minute, second);
}

void ws2812_init(void) {
    debug_print("ws2812_init: Starting LED initialization\n");
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    debug_print("ws2812_init: PIO program added at offset %d\n", offset);
    ws2812_sm = pio_claim_unused_sm(ws2812_pio, true);
    debug_print("ws2812_init: Claimed SM %d\n", ws2812_sm);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, WS2812_PIN, 800000, false);
    debug_print("ws2812_init: PIO program initialized\n");
    
    for (int i = 0; i < 3; i++) {
        set_ws2812(255, 0, 0);
        sleep_ms(200);
        set_ws2812(0, 255, 0);
        sleep_ms(200);
        set_ws2812(0, 0, 255);
        sleep_ms(200);
    }
    all_leds_off();
    debug_print("ws2812_init: LED initialization completed\n");
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(g) << 24) |
           ((uint32_t)(r) << 16) |
           ((uint32_t)(b) << 8);
}

void set_ws2812(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, urgb_u32(r, g, b));
}

void all_leds_off(void) {
    set_ws2812(0, 0, 0);
}

void update_time(void) {
    static uint32_t update_count = 0;
    uint64_t now = to_us_since_boot(get_absolute_time());
    uint64_t elapsed = now - last_time_update;
    
    if (elapsed >= 1000000) {
        current_second++;
        if (current_second >= 60) {
            current_second = 0;
            current_minute++;
            if (current_minute >= 60) {
                current_minute = 0;
                current_hour++;
                if (current_hour >= 24) {
                    current_hour = 0;
                }
                debug_print("update_time: Hour changed to %02d\n", current_hour);
            }
            debug_print("update_time: Minute changed to %02d\n", current_minute);
        }
        last_time_update += 1000000;
        if (update_count % 10 == 0) {
            debug_print("update_time: Time is %02d:%02d:%02d (count=%d)\n", current_hour, current_minute, current_second, update_count);
        }
        update_count++;
    }
}

time_encoded_t encode_time(void) {
    update_time();
    time_encoded_t encoded;
    
    if (current_hour == 0 || current_hour == 12) {
        encoded.n_hour = 12;
    } else {
        encoded.n_hour = current_hour % 12;
    }
    
    encoded.n_quarter = current_minute / 15;
    encoded.n_minute_rem = current_minute % 15;
    
    return encoded;
}

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

const char* state_names[] = {"IDLE", "BLINK_HOUR", "TRANSITION_1", "BLINK_QUARTER", "TRANSITION_2", "BLINK_MINUTE"};

void fsm_update(fsm_t *fsm) {
    static uint32_t last_log_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    
    // 在时间同步之前，显示网络状态
    if (!time_synced) {
        // 日志节流：每秒最多输出一次
        if (now - last_log_time >= 1000) {
            debug_print("fsm_update: Time not synced, network_status=%d\n", network_status);
            last_log_time = now;
        }
        if (now % 1000 < 200) {
            switch (network_status) {
                case 1: set_ws2812(0, 0, 255); break; // link up - blue
                case 2: set_ws2812(255, 0, 255); break; // DHCP ready - purple
                case 3: set_ws2812(0, 255, 0); break; // NTP OK - green
                default: set_ws2812(255, 0, 0); break; // no link - red
            }
        } else {
            all_leds_off();
        }
        return;
    }
    
    switch (fsm->current_state) {
        case STATE_IDLE:
            debug_print("fsm_update: STATE_IDLE, last_display_time=%llu, now_us=%llu\n", last_display_time, now_us);
            if (now_us - last_display_time >= 3000000) {
                debug_print("fsm_update: Starting display after 3 seconds idle\n");
                start_display();
                last_display_time = now_us;
            }
            break;
            
        case STATE_BLINK_HOUR:
            debug_print("fsm_update: STATE_BLINK_HOUR, blink_count=%d, blink_total=%d, led_on=%d\n", fsm->blink_count, fsm->blink_total, fsm->led_on);
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 800) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 255, 0);
                    debug_print("fsm_update: LED ON (green) for hour blink\n");
                } else if (fsm->led_on && elapsed >= 800) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                    debug_print("fsm_update: LED OFF, blink_count=%d\n", fsm->blink_count);
                    
                    if (fsm->blink_count % 3 == 0 && fsm->blink_count < fsm->blink_total) {
                        fsm->transition_active = true;
                        fsm->transition_start = now;
                        debug_print("fsm_update: Long pause after 3 blinks\n");
                    }
                }
                
                if (fsm->transition_active && (now - fsm->transition_start >= 1500)) {
                    fsm->transition_active = false;
                    fsm->last_tick = now;
                    debug_print("fsm_update: Long pause ended\n");
                }
            } else {
                fsm->transition_start = now;
                fsm->current_state = STATE_TRANSITION_1;
                all_leds_off();
                debug_print("fsm_update: Transition to STATE_TRANSITION_1\n");
            }
            break;
            
        case STATE_TRANSITION_1:
            debug_print("fsm_update: STATE_TRANSITION_1, elapsed=%d ms\n", now - fsm->transition_start);
            if (now - fsm->transition_start >= 1500) {
                debug_print("fsm_update: Transition 1 complete, n_quarter=%d, n_minute_rem=%d\n", current_time.n_quarter, current_time.n_minute_rem);
                if (current_time.n_quarter == 0) {
                    if (current_time.n_minute_rem == 0) {
                        fsm->current_state = STATE_IDLE;
                        debug_print("fsm_update: Transition to STATE_IDLE (no quarters, no minutes)\n");
                    } else {
                        fsm->blink_count = 0;
                        fsm->blink_total = current_time.n_minute_rem;
                        fsm->led_on = false;
                        fsm->last_tick = now;
                        fsm->current_state = STATE_BLINK_MINUTE;
                        debug_print("fsm_update: Transition to STATE_BLINK_MINUTE (total=%d)\n", fsm->blink_total);
                    }
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_quarter;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_QUARTER;
                    debug_print("fsm_update: Transition to STATE_BLINK_QUARTER (total=%d)\n", fsm->blink_total);
                }
            }
            break;
            
        case STATE_BLINK_QUARTER:
            debug_print("fsm_update: STATE_BLINK_QUARTER, blink_count=%d, blink_total=%d, led_on=%d\n", fsm->blink_count, fsm->blink_total, fsm->led_on);
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 500) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(255, 0, 0);
                    debug_print("fsm_update: LED ON (red) for quarter blink\n");
                } else if (fsm->led_on && elapsed >= 500) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                    debug_print("fsm_update: LED OFF, blink_count=%d\n", fsm->blink_count);
                }
            } else {
                fsm->transition_start = now;
                fsm->current_state = STATE_TRANSITION_2;
                debug_print("fsm_update: Transition to STATE_TRANSITION_2\n");
            }
            break;
            
        case STATE_TRANSITION_2:
            debug_print("fsm_update: STATE_TRANSITION_2, elapsed=%d ms\n", now - fsm->transition_start);
            if (now - fsm->transition_start >= 1500) {
                debug_print("fsm_update: Transition 2 complete, n_minute_rem=%d\n", current_time.n_minute_rem);
                if (current_time.n_minute_rem == 0) {
                    fsm->current_state = STATE_IDLE;
                    debug_print("fsm_update: Transition to STATE_IDLE (no minutes)\n");
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_minute_rem;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_MINUTE;
                    debug_print("fsm_update: Transition to STATE_BLINK_MINUTE (total=%d)\n", fsm->blink_total);
                }
            }
            break;
            
        case STATE_BLINK_MINUTE:
            debug_print("fsm_update: STATE_BLINK_MINUTE, blink_count=%d, blink_total=%d, led_on=%d\n", fsm->blink_count, fsm->blink_total, fsm->led_on);
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 400) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 0, 255);
                    debug_print("fsm_update: LED ON (blue) for minute blink\n");
                } else if (fsm->led_on && elapsed >= 400) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                    debug_print("fsm_update: LED OFF, blink_count=%d\n", fsm->blink_count);
                }
            } else {
                fsm->current_state = STATE_IDLE;
                debug_print("fsm_update: Transition to STATE_IDLE (minute blinks complete)\n");
            }
            break;
    }
}

int main(void) {
    ws2812_init();
    tusb_init();
    
    debug_print("=== LED Clock Booting ===\n");
    debug_print("TinyUSB initialized\n");
    
    net_init();
    debug_print("Network initialized\n");
    
    fsm_init(&fsm);
    
    last_time_update = to_us_since_boot(get_absolute_time());
    last_display_time = last_time_update;
    
    debug_print("FSM initialized\n");
    debug_print("Initial time: %02d:%02d:%02d\n", current_hour, current_minute, current_second);
    debug_print("Entering main loop...\n");
    
    uint32_t log_count = 0;
    uint64_t last_log_time = to_us_since_boot(get_absolute_time());
    
    while (true) {
        tud_task();
        update_time();
        net_task();
        fsm_update(&fsm);
        
        uint64_t now = to_us_since_boot(get_absolute_time());
        if (now - last_log_time >= 1000000) {
            debug_print("Loop %d: Time=%02d:%02d:%02d\n", log_count++, current_hour, current_minute, current_second);
            last_log_time = now;
        }
        
        tight_loop_contents();
    }
    
    return 0;
}
