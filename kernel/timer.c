#include "timer.h"
#include "idt.h"
#include "io.h"
#include "serial.h"
#include "sched.h"

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_freq = 0;

void timer_tick(void) {
    timer_ticks++;
}

static void timer_callback(registers_t* regs) {
    (void)regs;
    timer_tick();
}


void timer_init(uint32_t frequency) {
    timer_freq = frequency;
    // Register the handler (IRQ 0 is mapped to 0x20 in our IDT)
    idt_register_handler(0x20, timer_callback);

    // PIT I/O ports: 0x43 (Command), 0x40 (Channel 0)
    // Command 0x36: Square wave generator, LSB/MSB, Channel 0
    uint32_t divisor = 1193180 / frequency;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

void timer_sleep(uint64_t ms) {
    // Use HLT-based sleep instead of busy loop for efficiency
    // This allows other threads/CPUs to run while sleeping
    uint64_t start_ticks = timer_ticks;
    uint64_t ticks_to_wait = (ms * timer_freq) / 1000u;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;

    while (timer_ticks < start_ticks + ticks_to_wait) {
        __asm__ __volatile__("hlt");
    }
}

void timer_sleep_blocking(uint64_t ms) {
    // Scheduler-aware sleep that yields to other threads
    // Use this when you want the scheduler to run other threads
    // while this one sleeps
    int tid = scheduler_current_thread_id();
    scheduler_thread_sleep(tid, ms);
    
    // Busy-wait for the sleep to complete while other threads run
    uint64_t start_ticks = timer_ticks;
    uint64_t ticks_to_wait = (ms * timer_freq) / 1000u;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;

    while (timer_ticks < start_ticks + ticks_to_wait) {
        __asm__ __volatile__("hlt");
    }
}
