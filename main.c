#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/time.h"
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
static bool display_complete = false;

void rtc_setup(void) {
    last_time_update = to_us_since_boot(get_absolute_time());
    last_display_time = last_time_update;
}

void gpio_setup(void) {
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
           ((uint32_t)(b) << 16) |
           ((uint32_t)(r) << 8);
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
    fsm->breath_step = 0;
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
    fsm.breath_step = 0;
    fsm.last_tick = to_ms_since_boot(get_absolute_time());
    fsm.current_state = STATE_BLINK_HOUR;
    display_complete = false;
}

void fsm_update(fsm_t *fsm) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    
    switch (fsm->current_state) {
        case STATE_IDLE:
            if (!display_complete && (now_us - last_display_time >= 10000000)) {
                start_display();
                last_display_time = now_us;
            }
            break;
            
        case STATE_BLINK_HOUR:
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 500) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    fsm->breath_step = 0;
                } else if (fsm->led_on && elapsed >= 500) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    
                    if (fsm->blink_count % 3 == 0 && fsm->blink_count < fsm->blink_total) {
                        fsm->transition_active = true;
                        fsm->transition_start = now;
                    }
                }
                
                if (fsm->led_on) {
                    uint32_t breath_elapsed = now - fsm->last_tick;
                    int16_t breath_val = (sin((breath_elapsed * 3.14159f) / 500.0f) * 127) + 128;
                    if (breath_val < 0) breath_val = 0;
                    if (breath_val > 255) breath_val = 255;
                    set_ws2812(breath_val, 0, 0);
                } else {
                    all_leds_off();
                }
                
                if (fsm->transition_active && (now - fsm->transition_start >= 1000)) {
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
                        display_complete = true;
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
                
                if (!fsm->led_on && elapsed >= 250) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 255, 0);
                } else if (fsm->led_on && elapsed >= 250) {
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
                    display_complete = true;
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
                
                if (!fsm->led_on && elapsed >= 125) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(0, 0, 255);
                } else if (fsm->led_on && elapsed >= 125) {
                    fsm->led_on = false;
                    fsm->blink_count++;
                    fsm->last_tick = now;
                    all_leds_off();
                }
            } else {
                fsm->current_state = STATE_IDLE;
                display_complete = true;
            }
            break;
    }
}

int main(void) {
    stdio_init_all();
    rtc_setup();
    gpio_setup();
    ws2812_init();
    fsm_init(&fsm);
    
    while (true) {
        fsm_update(&fsm);
        tight_loop_contents();
    }
    
    return 0;
}
