#ifndef LED_CLOCK_H
#define LED_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

#define WS2812_PIN 16
#define WS2812_NUM_LEDS 1

typedef enum {
    STATE_IDLE,
    STATE_BLINK_HOUR,
    STATE_BLINK_QUARTER,
    STATE_BLINK_MINUTE,
    STATE_TRANSITION_1,
    STATE_TRANSITION_2
} state_t;

typedef struct {
    uint8_t n_hour;
    uint8_t n_quarter;
    uint8_t n_minute_rem;
} time_encoded_t;

typedef struct {
    state_t current_state;
    uint8_t blink_count;
    uint8_t blink_total;
    uint8_t breath_step;
    bool led_on;
    uint32_t last_tick;
    bool transition_active;
    uint32_t transition_start;
} fsm_t;

void rtc_setup(void);
void gpio_setup(void);
void ws2812_init(void);
void fsm_init(fsm_t *fsm);
void fsm_update(fsm_t *fsm);
time_encoded_t encode_time(void);
void set_ws2812(uint8_t r, uint8_t g, uint8_t b);
void all_leds_off(void);

#endif
