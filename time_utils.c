#include <stdint.h>
#include "pico/time.h"
#include "led_clock.h"

extern uint32_t current_hour;
extern uint32_t current_minute;
extern uint32_t current_second;
extern uint64_t last_time_update;

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