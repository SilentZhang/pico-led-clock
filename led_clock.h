#ifndef LED_CLOCK_H
#define LED_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

#define WS2812_PIN 16

typedef enum {
    STATE_IDLE,
    STATE_BLINK_HOUR,
    STATE_TRANSITION_1,
    STATE_BLINK_QUARTER,
    STATE_TRANSITION_2,
    STATE_BLINK_MINUTE
} state_t;

typedef struct {
    state_t current_state;
    uint32_t blink_count;
    uint32_t blink_total;
    bool led_on;
    uint32_t last_tick;
    bool transition_active;
    uint32_t transition_start;
} fsm_t;

typedef struct {
    uint32_t n_hour;
    uint32_t n_quarter;
    uint32_t n_minute_rem;
} time_encoded_t;

void ws2812_init(void);
void set_ws2812(uint8_t r, uint8_t g, uint8_t b);
void all_leds_off(void);
void fsm_init(fsm_t *fsm);
void fsm_update(fsm_t *fsm);
void start_display(void);

void net_init(void);
void net_task(void);
bool net_is_connected(void);
bool net_time_synced(void);

#endif
