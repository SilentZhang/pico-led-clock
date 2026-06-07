#include <stdint.h>
#include <string.h>
#include "tusb.h"

extern uint32_t current_hour;
extern uint32_t current_minute;
extern uint32_t current_second;

static const char* ascii_digit_lines[] = {
    " ####### \n"
    "##     ##\n"
    "##     ##\n"
    "##     ##\n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n",
    "   ###   \n"
    "  #####  \n"
    "   ###   \n"
    "   ###   \n"
    "   ###   \n"
    "   ###   \n"
    "   ###   \n",
    " ####### \n"
    "##     ##\n"
    "       ##\n"
    " ####### \n"
    "##       \n"
    "##       \n"
    " ####### \n",
    " ####### \n"
    "##     ##\n"
    "       ##\n"
    " ####### \n"
    "       ##\n"
    "##     ##\n"
    " ####### \n",
    "##     ##\n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n"
    "       ##\n"
    "       ##\n"
    "       ##\n",
    " ####### \n"
    "##       \n"
    "##       \n"
    " ####### \n"
    "       ##\n"
    "##     ##\n"
    " ####### \n",
    " ####### \n"
    "##       \n"
    "##       \n"
    " ####### \n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n",
    " ####### \n"
    "       ##\n"
    "       ##\n"
    "       ##\n"
    "       ##\n"
    "       ##\n"
    "       ##\n",
    " ####### \n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n",
    " ####### \n"
    "##     ##\n"
    "##     ##\n"
    " ####### \n"
    "       ##\n"
    "##     ##\n"
    " ####### \n"
};

static const char* ascii_colon_lines = 
    "   ##    \n"
    "         \n"
    "         \n"
    "   ##    \n"
    "         \n"
    "         \n"
    "         \n";

#ifdef NDEBUG
void print_ascii_time(void) {
    uint32_t h = current_hour;
    uint32_t m = current_minute;
    uint32_t s = current_second;
    
    int digits[6] = {
        h / 10, h % 10,
        m / 10, m % 10,
        s / 10, s % 10
    };
    
    for (int line = 0; line < 7; line++) {
        char buf[256];
        int pos = 0;
        
        int line_start = line * 10;
        
        const char* d = ascii_digit_lines[digits[0]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        buf[pos++] = ' ';
        
        d = ascii_digit_lines[digits[1]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        buf[pos++] = ' ';
        
        for (int i = 0; i < 9; i++) {
            buf[pos++] = ascii_colon_lines[line_start + i];
        }
        buf[pos++] = ' ';
        
        d = ascii_digit_lines[digits[2]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        buf[pos++] = ' ';
        
        d = ascii_digit_lines[digits[3]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        buf[pos++] = ' ';
        
        for (int i = 0; i < 9; i++) {
            buf[pos++] = ascii_colon_lines[line_start + i];
        }
        buf[pos++] = ' ';
        
        d = ascii_digit_lines[digits[4]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        buf[pos++] = ' ';
        
        d = ascii_digit_lines[digits[5]];
        for (int i = 0; i < 9; i++) {
            buf[pos++] = d[line_start + i];
        }
        
        buf[pos++] = '\n';
        buf[pos] = '\0';
        
        int sent = 0;
        while (sent < pos) {
            int n = tud_cdc_write(buf + sent, pos - sent);
            if (n > 0) {
                sent += n;
            }
            tud_task();
        }
    }
    
    tud_cdc_write("\n\n", 2);
    tud_cdc_write_flush();
}
#endif