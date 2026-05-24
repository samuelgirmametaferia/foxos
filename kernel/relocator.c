#include "relocator.h"
#include "timer.h"
#include "memory.h"
#include "relocation.h"
#include "serial.h"

static relocator_status_t state;

static void serial_u64(uint64_t v) {
    char buf[32]; int n=0; if (v==0) { buf[n++]='0'; buf[n]=0; serial_write(buf); return; }
    char tmp[32]; int t=0; while(v){ tmp[t++]=(char)('0'+(v%10)); v/=10; }
    while(t--) buf[n++]=tmp[t]; buf[n]=0; serial_write(buf);
}

void relocator_init(void) {
    state.enabled = 0;
    state.threshold_pages = 64; /* default low free pages threshold */
    state.interval_ms = 5000; /* default 5s */
    state.last_tick_ms = timer_get_ticks();
    state.running = 0;
    state.last_moved = 0;
}

void relocator_start(void) {
    state.enabled = 1;
}

void relocator_stop(void) {
    state.enabled = 0;
}

void relocator_set_threshold(uint64_t pages) {
    state.threshold_pages = pages;
}

void relocator_set_interval(uint64_t ms) {
    state.interval_ms = ms;
}

relocator_status_t relocator_get_status(void) {
    return state;
}

void relocator_poll(void) {
    if (!state.enabled) return;
    uint64_t now_ticks = timer_get_ticks();
    /* timer_get_ticks increments at initialization frequency (e.g., 100Hz). Convert to ms.
       ticks / freq * 1000 = ms; but we don't have freq access; assume timer_freq=100 set in timer_init.
       Use ticks * 10 ms when freq==100. To avoid depending on freq, treat ticks as 10ms units when timer_init(100) used.
    */
    uint64_t now_ms = now_ticks * 10; /* safe given timer_init(100) used elsewhere */
    if (now_ms < state.last_tick_ms + state.interval_ms) return;
    state.last_tick_ms = now_ms;

    uint64_t free_pages = pmm_free_pages();
    if (free_pages <= state.threshold_pages) {
        serial_write("[relocator] low free pages: "); serial_u64(free_pages); serial_writeln("");
        state.running = 1;
        int moved = relocation_compact_multi(3);
        state.last_moved = moved;
        state.running = 0;
        serial_write("[relocator] moved: "); serial_u64((uint64_t)moved); serial_writeln("");
    }
}
