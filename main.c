#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "led_clock.h"

fsm_t fsm;
time_encoded_t current_time;

uint32_t current_hour = 7;
uint32_t current_minute = 37;
uint32_t current_second = 0;
uint64_t last_time_update = 0;
uint64_t last_display_time = 0;
bool time_synced = false;
uint8_t network_status = 0;

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

void debug_print(const char *format, ...) {
#ifndef NDEBUG
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
#endif
}

int main(void) {
    ws2812_init();
    
    tusb_init();
    
    debug_print("=== LED Clock Booting ===\n");
    debug_print("TinyUSB initialized\n");
    
    net_init();
    debug_print("Network initialized\n");
    
    fsm_init(&fsm);
    offline_time_set_init();
    
    last_time_update = to_us_since_boot(get_absolute_time());
    last_display_time = last_time_update;
    
    debug_print("FSM initialized\n");
    debug_print("Initial time: %02d:%02d:%02d\n", current_hour, current_minute, current_second);
    debug_print("Entering main loop...\n");
    
    uint32_t log_count = 0;
    uint64_t last_log_time = to_us_since_boot(get_absolute_time());
    uint64_t last_ascii_time = to_us_since_boot(get_absolute_time());
    
    while (true) {
        tud_task();
        update_time();
        net_task();
        offline_time_set_update();
        fsm_update(&fsm);
        
        uint64_t now = to_us_since_boot(get_absolute_time());
        
#ifndef NDEBUG
        if (now - last_log_time >= 1000000) {
            debug_print("Loop %d: Time=%02d:%02d:%02d\n", log_count++, current_hour, current_minute, current_second);
            last_log_time = now;
        }
#else
        if (now - last_ascii_time >= 10000000) {
            print_ascii_time();
            last_ascii_time = now;
        }
#endif
        
        tight_loop_contents();
    }
    
    return 0;
}