#pragma once
#include <stdint.h>
#include "boot.h"

void console_init(const boot_info_t* boot);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
void console_putc(char c);
void console_write(const char* s);
void console_writeln(const char* s);
