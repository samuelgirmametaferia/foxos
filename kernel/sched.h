#pragma once
#include <stdint.h>
#include "idt.h"

void scheduler_init(void);
int scheduler_create(void (*entry)(void));
void scheduler_set_idle(void (*entry)(void));
void scheduler_start(void);
void scheduler_stop(void);
registers_t* scheduler_tick(registers_t* regs);
