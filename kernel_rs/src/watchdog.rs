static mut LAST_TICK: u64 = 0;
static mut HANG_COUNT: u32 = 0;

extern "C" {
    fn timer_get_ticks() -> u64;
    fn idt_panic_handler(regs: *const core::ffi::c_void, message: *const u8);
}

#[no_mangle]
pub unsafe extern "C" fn rust_watchdog_start() {
    crate::info!("Watchdog thread started");
    loop {
        let current_ticks = timer_get_ticks();
        if current_ticks == LAST_TICK {
            HANG_COUNT += 1;
        } else {
            LAST_TICK = current_ticks;
            HANG_COUNT = 0;
        }

        if HANG_COUNT > 500 { // ~5 seconds at 100Hz
            crate::error!("WATCHDOG: System hang detected! No timer ticks for 5 seconds.");
            let msg = b"Watchdog: System hang detected\0";
            idt_panic_handler(core::ptr::null(), msg.as_ptr());
        }

        // Sleep for ~100ms using a spin loop (rough estimate)
        for _ in 0..10_000_000 {
            core::hint::spin_loop();
        }
        
        // Use blocking sleep instead of yield if available, 
        // but watchdog should be resilient.
        extern "C" { 
            fn scheduler_yield();
            fn scheduler_thread_sleep(tid: i32, ms: u64);
            fn scheduler_current_thread_id() -> i32;
        }
        scheduler_thread_sleep(scheduler_current_thread_id(), 100);
    }
}
