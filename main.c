#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
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

// 离线时间设置 - 按钮状态机
typedef enum {
    BTN_WAIT_FIRST,      // 等待第一轮按键（小时）
    BTN_WAIT_SECOND,     // 等待第二轮按键（刻钟）
    BTN_WAIT_THIRD,      // 等待第三轮按键（残余分钟）
    BTN_DONE            // 设置完成
} btn_set_state_t;

static btn_set_state_t btn_set_state = BTN_WAIT_FIRST;
static uint32_t btn_count_hour = 0;
static uint32_t btn_count_quarter = 0;
static uint32_t btn_count_minute = 0;
static uint64_t btn_last_press_time = 0;
static bool btn_was_pressed = false;
static uint32_t btn_this_press_count = 0;

void offline_time_set_init(void);
void offline_time_set_update(void);
void debug_print(const char *format, ...);

// 读取 BOOTSEL 按钮 - 必须在 RAM 中运行
// 参考: https://github.com/raspberrypi/pico-examples/blob/master/picoboard/button/button.c
bool __no_inline_not_in_flash_func(read_bootsel_button)() {
    const uint CS_PIN_INDEX = 1; // QSPI_SS 的索引
    
    // 必须禁用中断，因为中断处理程序可能在 flash 中
    uint32_t flags = save_and_disable_interrupts();
    
    // 将 chip select 设置为 Hi-Z (输出禁用)
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    // 等待一小段时间让引脚稳定
    for (volatile int i = 0; i < 1000; ++i);
    
    // 使用 SIO 的 HI GPIO 寄存器读取 QSPI 引脚
    // 按钮按下时为低电平
    #if PICO_RP2040
    #define CS_BIT (1u << 1)
    #else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
    #endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);
    
    // 恢复 chip select 的状态
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    
    restore_interrupts(flags);
    return button_state;
}

void offline_time_set_init(void) {
    btn_set_state = BTN_WAIT_FIRST;
    btn_count_hour = 0;
    btn_count_quarter = 0;
    btn_count_minute = 0;
    btn_last_press_time = 0;
    btn_was_pressed = false;
    btn_this_press_count = 0;
}

void offline_time_set_update(void) {
    uint64_t now = to_us_since_boot(get_absolute_time());
    // 使用特殊方法读取 BOOTSEL 按钮
    bool btn_pressed = read_bootsel_button();
    
    // 每秒输出一次按钮状态用于调试
    static uint64_t last_btn_debug = 0;
    if (now - last_btn_debug >= 1000000) {
        debug_print("BOOTSEL: pressed=%d\n", btn_pressed);
        last_btn_debug = now;
    }
    
    switch (btn_set_state) {
        case BTN_WAIT_FIRST:
            if (btn_pressed && !btn_was_pressed) {
                // 按键按下，计数增加
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Hour press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                // 按键刚刚释放，更新时间为释放时间
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                // 按键释放后，检查空闲时间
                uint32_t idle_time = now - btn_last_press_time;
                
                // 超过5秒，重置
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                // 超过1秒，结束这轮
                if (idle_time > 1000000) {
                    btn_count_hour = btn_this_press_count;
                    if (btn_count_hour > 12) btn_count_hour = 12;
                    btn_set_state = BTN_WAIT_SECOND;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                    debug_print("offline: Hour=%d, waiting for quarter (1-5s)\n", btn_count_hour);
                }
            }
            break;
            
        case BTN_WAIT_SECOND:
            // 检查空闲超时（超过5秒丢弃）
            if (!btn_pressed && !btn_was_pressed && btn_this_press_count == 0) {
                uint32_t idle_time = now - btn_last_press_time;
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
            }
            
            if (btn_pressed && !btn_was_pressed) {
                // 按键按下，计数增加
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Quarter press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                // 按键刚刚释放，更新时间为释放时间
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                // 按键释放后，检查空闲时间
                uint32_t idle_time = now - btn_last_press_time;
                
                // 超过5秒，重置
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                // 超过1秒，结束这轮
                if (idle_time > 1000000) {
                    // 按住超过3次计为0刻钟
                    if (btn_this_press_count > 3) {
                        btn_count_quarter = 0;
                    } else {
                        btn_count_quarter = btn_this_press_count;
                    }
                    btn_set_state = BTN_WAIT_THIRD;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                    debug_print("offline: Quarter=%d, waiting for minute (1-5s)\n", btn_count_quarter);
                }
            }
            break;
            
        case BTN_WAIT_THIRD:
            // 检查空闲超时（超过5秒丢弃）
            if (!btn_pressed && !btn_was_pressed && btn_this_press_count == 0) {
                uint32_t idle_time = now - btn_last_press_time;
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
            }
            
            if (btn_pressed && !btn_was_pressed) {
                // 按键按下，计数增加
                btn_this_press_count++;
                btn_last_press_time = now;
                debug_print("offline: Minute press count=%d\n", btn_this_press_count);
            } else if (!btn_pressed && btn_was_pressed) {
                // 按键刚刚释放，更新时间为释放时间
                btn_last_press_time = now;
            } else if (!btn_pressed && !btn_was_pressed && btn_this_press_count > 0) {
                // 按键释放后，检查空闲时间
                uint32_t idle_time = now - btn_last_press_time;
                
                // 超过5秒，重置
                if (idle_time > 5000000) {
                    debug_print("offline: Idle timeout (>5s), resetting\n");
                    offline_time_set_init();
                    break;
                }
                
                // 超过1秒，结束这轮，设置时间
                if (idle_time > 1000000) {
                    // 按住超过15次计为0分钟
                    if (btn_this_press_count > 15) {
                        btn_count_minute = 0;
                    } else {
                        btn_count_minute = btn_this_press_count;
                    }
                    
                    // 计算实际时间
                    current_hour = btn_count_hour;
                    current_minute = btn_count_quarter * 15 + btn_count_minute;
                    if (current_minute >= 60) current_minute = 59;
                    current_second = 0;
                    last_time_update = now;
                    time_synced = true;
                    
                    debug_print("offline: Time set to %02d:%02d:%02d\n", current_hour, current_minute, current_second);
                    
                    btn_set_state = BTN_DONE;
                    btn_last_press_time = now;
                    btn_this_press_count = 0;
                }
            }
            break;
            
        case BTN_DONE:
            // 等待5秒后可以开始新的设置
            if (now - btn_last_press_time > 5000000) {
                offline_time_set_init();
                debug_print("offline: Ready for new setting\n");
            }
            break;
    }
    
    btn_was_pressed = btn_pressed;
}

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
            if (now_us - last_display_time >= 3000000) {
                debug_print("fsm_update: Starting display after 3 seconds idle\n");
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
                    set_ws2812(0, 255, 0);
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
                debug_print("fsm: Hour blinks done, transitioning\n");
            }
            break;
            
        case STATE_TRANSITION_1:
            if (now - fsm->transition_start >= 1500) {
                if (current_time.n_quarter == 0) {
                    if (current_time.n_minute_rem == 0) {
                        fsm->current_state = STATE_IDLE;
                        debug_print("fsm: Display complete, returning to idle\n");
                    } else {
                        fsm->blink_count = 0;
                        fsm->blink_total = current_time.n_minute_rem;
                        fsm->led_on = false;
                        fsm->last_tick = now;
                        fsm->current_state = STATE_BLINK_MINUTE;
                        debug_print("fsm: Starting minute blinks (%d)\n", fsm->blink_total);
                    }
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_quarter;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_QUARTER;
                    debug_print("fsm: Starting quarter blinks (%d)\n", fsm->blink_total);
                }
            }
            break;
            
        case STATE_BLINK_QUARTER:
            if (fsm->blink_count < fsm->blink_total) {
                uint32_t elapsed = now - fsm->last_tick;
                
                if (!fsm->led_on && elapsed >= 500) {
                    fsm->led_on = true;
                    fsm->last_tick = now;
                    set_ws2812(255, 0, 0);
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
                    debug_print("fsm: Display complete, returning to idle\n");
                } else {
                    fsm->blink_count = 0;
                    fsm->blink_total = current_time.n_minute_rem;
                    fsm->led_on = false;
                    fsm->last_tick = now;
                    fsm->current_state = STATE_BLINK_MINUTE;
                    debug_print("fsm: Starting minute blinks (%d)\n", fsm->blink_total);
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
                debug_print("fsm: Display complete, returning to idle\n");
            }
            break;
    }
}

int main(void) {
    ws2812_init();
    
    // BOOTSEL 按钮连接到 QSPI_SS，使用特殊方法读取
    // 不需要 GPIO 初始化
    debug_print("BOOTSEL button ready (using QSPI_SS method)\n");
    
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
    
    while (true) {
        tud_task();
        update_time();
        net_task();
        offline_time_set_update();  // 处理离线时间设置按钮
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
