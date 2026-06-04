#pragma once
#include <stdint.h>

void u64_to_dec(uint64_t v, char* buf);
void u32_to_dec(uint32_t v, char* buf);
void u64_to_hex(uint64_t v, char* buf);
void u32_to_hex(uint32_t v, char* buf);
int str_len(const char* s);
int streq(const char* a, const char* b);
int strcaseeq(const char* a, const char* b);
int startswith(const char* s, const char* p);
void str_copy(char* dst, const char* src, int cap);
char to_lower(char c);
uint64_t dec_to_u64(const char* s);

int memcmp(const void* a, const void* b, uint64_t n);
void memzero(void* ptr, uint32_t size);
