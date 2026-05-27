#pragma once
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char* s);
void serial_writeln(const char* s);
void serial_u64(uint64_t v);
void serial_u32(uint32_t v);
