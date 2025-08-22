#pragma once
#include <stdint.h>

void console_init(void);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
void console_putc(char c);
void console_write(const char* s);
void console_writeln(const char* s);
