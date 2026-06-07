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

// 串口手动设置时间：接收 HHMMSS 格式的时间
static bool set_time_from_serial(void) {
    if (!tud_cdc_available()) {
        return false;
    }
    
    static char input_buf[16] = {0};
    static uint8_t buf_pos = 0;
    
    while (tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) == 1) {
            if (c == '\n' || c == '\r') {
                // 收到换行，处理命令
                if (buf_pos == 6) {
                    input_buf[6] = '\0';
                    
                    // 解析 HHMMSS
                    int h = (input_buf[0] - '0') * 10 + (input_buf[1] - '0');
                    int m = (input_buf[2] - '0') * 10 + (input_buf[3] - '0');
                    int s = (input_buf[4] - '0') * 10 + (input_buf[5] - '0');
                    
                    // 验证格式：H 0-23, M 0-59, S 0-59
                    if (h <= 23 && m <= 59 && s <= 59) {
                        current_hour = h;
                        current_minute = m;
                        current_second = s;
                        last_time_update = to_us_since_boot(get_absolute_time());
                        time_synced = true;
                        
                        // 在release模式下也输出应答
                        char resp[32];
                        int len = snprintf(resp, sizeof(resp), "[OK] Time set to %02d:%02d:%02d\n", 
                                         current_hour, current_minute, current_second);
                        tud_cdc_write(resp, len);
                        tud_cdc_write_flush();
                    } else {
                        tud_cdc_write("[ERR] Invalid time format\n", 25);
                        tud_cdc_write_flush();
                    }
                }
                buf_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (buf_pos < 15 && c >= '0' && c <= '9') {
                input_buf[buf_pos++] = c;
            }
        }
    }
    return false;
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
        set_time_from_serial();
        
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