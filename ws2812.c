#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "led_clock.h"
#include "ws2812.pio.h"

static PIO ws2812_pio = pio0;
static uint ws2812_sm;

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(g) << 24) |
           ((uint32_t)(r) << 16) |
           ((uint32_t)(b) << 8);
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

void set_ws2812(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, urgb_u32(r, g, b));
}

void all_leds_off(void) {
    set_ws2812(0, 0, 0);
}