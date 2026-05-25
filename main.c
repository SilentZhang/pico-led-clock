#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
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

static uint32_t current_hour = 7;
static uint32_t current_minute = 37;
static uint64_t last_time_update = 0;
static uint64_t last_display_time = 0;
static bool time_synced = false;
static uint8_t network_status = 0; // 0: no link, 1: link up, 2: dhcp ok, 3: ntp ok

void indicate_network_status(uint8_t status) {
    network_status = status;
}

void set_ntp_callback(uint32_t hour, uint32_t minute) {
    current_hour = hour;
    current_minute = minute;
    last_time_update = to_us_since_boot(get_absolute_time());
    time_synced = true;
    
    for (int i = 0; i < 2; i++) {
        set_ws2812(0, 255, 0);
        sleep_ms(100);
        all_leds_off();
        sleep_ms(100);
    }
}

void ws2812_init(void) {
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_sm = pio_claim_unused_sm(ws2812_pio, true);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, WS2812_PIN, 800000, false);
    
    for (int i = 0; i < 3; i++) {
        set_ws2812(255, 0, 0);
        sleep_ms(200);
        set_ws2812(0, 255, 0);
        sleep_ms(200);
        set_ws2812(0, 0, 255);
        sleep_ms(200);
    }
    all_leds_off();
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
    uint64_t now = to_us_since_boot(get_absolute_time());
    uint64_t elapsed = now - last_time_update;
    
    if (elapsed >= 60000000) {
        current_minute++;
        if (current_minute >= 60) {
            current_minute = 0;
            current_hour++;
            if (current_hour >= 24) {
                current_hour = 0;
            }
        }
        last_time_update += 60000000;
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

void fsm_update(fsm_t *fsm) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    
    // 在时间同步之前，显示网络状态
    if (!time_synced) {
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
            if (now_us - last_display_time >= 3000000) {
                start_display();
                last_display_time = now_us;
            }
            break;
            
        case STATE_BLINK_HOUR:
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 800) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(255, 0, 0);
                } else if (fsm->led_on && elapsed >= 800) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                    
                    if (fsm->blink_count % 3 == 0 && fsm->blink_count < fsm->blink_total) {
                        fsm->transition_active = true;
                        fsm->transition_start = now;
                    }
                }
                
                if (fsm->transition_active && (now - fsm->transition_start >= 1500)) {
                    fsm->transition_active = false;
                    fsm->last_tick = now;
                }
            } else {
                fsm->transition_start = now;
                fsm->current_state = STATE_TRANSITION_1;
                all_leds_off();
            }
            break;
            
        case STATE_TRANSITION_1:
            if (now - fsm->transition_start >= 1500) {
                if (current_time.n_quarter == 0) {
                    if (current_time.n_minute_rem == 0) {
                        fsm->current_state = STATE_IDLE;
                    } else {
                        fsm->blink_count = 0;
                        fsm->blink_total = current_time.n_minute_rem;
                        fsm->led_on = false;
                        fsm->last_tick = now;
                        fsm->current_state = STATE_BLINK_MINUTE;
                    }
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_quarter;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_QUARTER;
                }
            }
            break;
            
        case STATE_BLINK_QUARTER:
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 500) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 255, 0);
                } else if (fsm->led_on && elapsed >= 500) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                }
            } else {
                fsm->transition_start = now;
                fsm->current_state = STATE_TRANSITION_2;
            }
            break;
            
        case STATE_TRANSITION_2:
            if (now - fsm->transition_start >= 1500) {
                if (current_time.n_minute_rem == 0) {
                    fsm->current_state = STATE_IDLE;
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_minute_rem;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_MINUTE;
                }
            }
            break;
            
        case STATE_BLINK_MINUTE:
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 400) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 0, 255);
                } else if (fsm->led_on && elapsed >= 400) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                }
            } else {
                fsm->current_state = STATE_IDLE;
            }
            break;
    }
}

int main(void) {
    stdio_init_all();
    
    ws2812_init();
    tusb_init();  // 初始化 TinyUSB USB 设备
    net_init();
    fsm_init(&fsm);
    
    last_time_update = to_us_since_boot(get_absolute_time());
    last_display_time = last_time_update;
    
    while (true) {
        net_task();
        fsm_update(&fsm);
        tight_loop_contents();
    }
    
    return 0;
}
