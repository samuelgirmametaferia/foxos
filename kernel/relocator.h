#ifndef FOXOS_RELOCATOR_H
#define FOXOS_RELOCATOR_H

#include <stdint.h>

typedef struct {
    int enabled;
    uint64_t threshold_pages;
    uint64_t interval_ms;
    uint64_t last_tick_ms;
    int running;
    int last_moved;
} relocator_status_t;

void relocator_init(void);
void relocator_poll(void); /* call frequently from main loop */
void relocator_start(void);
void relocator_stop(void);
void relocator_set_threshold(uint64_t pages);
void relocator_set_interval(uint64_t ms);
relocator_status_t relocator_get_status(void);

#endif
