#pragma once
#include <stdint.h>

void timer_init(uint32_t frequency);
uint64_t timer_get_ticks(void);
void timer_sleep(uint64_t ms);
void timer_sleep_blocking(uint64_t ms);  // Scheduler-aware sleep
