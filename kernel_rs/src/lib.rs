#![no_std]

extern crate alloc;

mod allocator;
pub mod gcb;
pub mod console;
mod logging;
mod watchdog;
mod syscall;

use core::panic::PanicInfo;
use core::sync::atomic::Ordering;

pub use console::rust_console_putc;
pub use logging::LogLevel;

#[no_mangle]
pub unsafe extern "C" fn rust_init(gcb_ptr: *mut gcb::Gcb) {
    if !gcb_ptr.is_null() && (*gcb_ptr).magic == gcb::GCB_MAGIC {
        gcb::GCB_PTR = gcb_ptr;
        console::init();
        info!("Rust kernel subsystem initialized");
    } else {
        loop {
            core::hint::spin_loop();
        }
    }
}

pub fn log_info(msg: &str) {
    info!("{}", msg);
}

#[repr(C)]
pub struct SysInfo {
    pub cpu_count: u32,
    pub total_mem_mb: u32,
    pub free_mem_mb: u32,
    pub process_count: u32,
    pub uptime_ms: u64,
    pub fb_width: u32,
    pub fb_height: u32,
    pub fb_stride: u32,
}

#[no_mangle]
pub unsafe extern "C" fn rust_get_system_info(info: *mut SysInfo) -> i32 {
    if info.is_null() { return -1; }
    
    let info = &mut *info;
    
    extern "C" {
        fn smp_cpu_count() -> u32;
        fn pmm_total_pages() -> u64;
        fn pmm_free_pages() -> u64;
        fn timer_get_ticks() -> u64;
        fn gpu_get_width() -> u32;
        fn gpu_get_height() -> u32;
        fn gpu_get_stride() -> u32;
    }

    info.cpu_count = smp_cpu_count();
    let total_pages = pmm_total_pages();
    let free_pages = pmm_free_pages();
    info.total_mem_mb = (total_pages * 4096 / 1024 / 1024) as u32;
    info.free_mem_mb = (free_pages * 4096 / 1024 / 1024) as u32;
    
    // Convert ticks to ms. Assuming 100Hz frequency from apic_timer_init(100, 34)
    info.uptime_ms = timer_get_ticks() * 10;
    
    info.fb_width = gpu_get_width();
    info.fb_height = gpu_get_height();
    info.fb_stride = gpu_get_stride();
    
    0
}

#[repr(C)]
struct Regs {
    rax: u64, rbx: u64, rcx: u64, rdx: u64, rsi: u64, rdi: u64, rbp: u64,
    r8: u64, r9: u64, r10: u64, r11: u64, r12: u64, r13: u64, r14: u64, r15: u64,
    int_no: u64, err_code: u64,
    rip: u64, cs: u64, rflags: u64, rsp: u64, ss: u64,
}

#[no_mangle]
pub unsafe extern "C" fn rust_increment_ticks() {
    if !gcb::GCB_PTR.is_null() {
        let val = (*gcb::GCB_PTR).global_ticks.fetch_add(1, Ordering::Relaxed) + 1;
        if val % 100 == 0 {
            extern "C" { fn serial_putc(c: u8); }
            serial_putc(b'.' as u8);
        }
    } else {
        static mut SPAMMED: u32 = 0;
        if SPAMMED < 10 {
            extern "C" { 
                fn serial_putc(c: u8); 
                fn serial_write(s: *const u8);
            }
            serial_putc(b'?' as u8);
            if SPAMMED == 0 {
                serial_write(b" [RUST] GCB_PTR is NULL in rust_increment_ticks\n\0".as_ptr());
            }
            SPAMMED += 1;
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_get_ticks() -> u64 {
    if gcb::GCB_PTR.is_null() { return 0; }
    (*gcb::GCB_PTR).global_ticks.load(Ordering::Relaxed)
}

#[no_mangle]
pub unsafe extern "C" fn rust_interrupt_handler(regs: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    let r = &mut *(regs as *mut Regs);

    if r.int_no < 32 {
        // Exception handling
        rust_exception_handler(regs, core::ptr::null());
        // rust_exception_handler doesn't return
        return regs;
    }

    // IRQ handling (starting at 32)
    if r.int_no >= 32 && r.int_no < 48 {
        // Call C handlers for now
        extern "C" {
            fn idt_dispatch_irq(regs: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
        }
        return idt_dispatch_irq(regs);
    }

    // IRQ 0 (PIT) or APIC timer
    if r.int_no == 32 || r.int_no == 34 {
        extern "C" {
            fn idt_dispatch_irq(regs: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
        }
        idt_dispatch_irq(regs);

        extern "C" { fn scheduler_tick(regs: *mut core::ffi::c_void) -> *mut core::ffi::c_void; }
        return scheduler_tick(regs);
    }

    // Custom interrupts
    if r.int_no == 0x22 {
        // Yield doesn't need EOI as it's not a hardware IRQ
        extern "C" { fn scheduler_tick(regs: *mut core::ffi::c_void) -> *mut core::ffi::c_void; }
        return scheduler_tick(regs);
    }

    regs
}

#[no_mangle]
pub unsafe extern "C" fn rust_exception_handler(regs: *const core::ffi::c_void, message: *const u8) {
    if regs.is_null() { return; }
    let r = &*(regs as *const Regs);
    let is_user = (r.cs & 3) != 0;
    
    extern "C" {
        fn scheduler_current_thread_id() -> i32;
        fn scheduler_get_thread_pid(tid: i32) -> u32;
    }
    let tid = scheduler_current_thread_id();
    let pid = scheduler_get_thread_pid(tid);

    let con = &raw mut console::CONSOLE;
    // Lock-free console output for exceptions
    (*con).panic_putc('\n');
    for _ in 0..60 { (*con).panic_putc('='); }
    (*con).panic_putc('\n');
    
    let title = if is_user { " *** USER MODE EXCEPTION ***" } else { " *** KERNEL MODE EXCEPTION ***" };
    for c in title.chars() { (*con).panic_putc(c); }
    (*con).panic_putc('\n');
    
    let mut print_str = |s: &str| {
        for c in s.chars() { (*con).panic_putc(c); }
    };

    print_str(" PID: ");
    let mut v = pid;
    if v == 0 { (*con).panic_putc('0'); }
    else {
        let mut buf = [0u8; 10];
        let mut i = 0;
        while v > 0 { buf[i] = (v % 10) as u8; v /= 10; i += 1; }
        while i > 0 { i -= 1; (*con).panic_putc((b'0' + buf[i]) as char); }
    }
    print_str("  TID: ");
    let mut v = tid as u32;
    if v == 0 { (*con).panic_putc('0'); }
    else {
        let mut buf = [0u8; 10];
        let mut i = 0;
        while v > 0 { buf[i] = (v % 10) as u8; v /= 10; i += 1; }
        while i > 0 { i -= 1; (*con).panic_putc((b'0' + buf[i]) as char); }
    }
    (*con).panic_putc('\n');

    if !message.is_null() {
        print_str(" Reason: ");
        let mut i = 0;
        while *message.add(i) != 0 {
            (*con).panic_putc(*message.add(i) as char);
            i += 1;
        }
        (*con).panic_putc('\n');
    }

    let mut print_reg = |name: &str, val: u64| {
        for c in name.chars() { (*con).panic_putc(c); }
        for _ in 0..(8 - name.len()) { (*con).panic_putc(' '); }
        (*con).panic_putc(':'); (*con).panic_putc(' ');
        let mut v = val;
        for i in (0..16).rev() {
            let digit = (v >> (i * 4)) & 0xF;
            (*con).panic_putc(if digit < 10 { (b'0' + digit as u8) as char } else { (b'A' + (digit - 10) as u8) as char });
        }
        (*con).panic_putc('\n');
    };

    extern "C" {
        fn read_cr2() -> u64;
    }
    let cr2 = read_cr2();
    print_reg("CR2", cr2);

    print_reg("RIP", r.rip);
    print_reg("CS", r.cs);
    print_reg("RFLAGS", r.rflags);
    print_reg("RSP", r.rsp);
    print_reg("RAX", r.rax);
    print_reg("RBX", r.rbx);
    print_reg("RCX", r.rcx);
    print_reg("RDX", r.rdx);
    print_reg("RSI", r.rsi);
    print_reg("RDI", r.rdi);
    print_reg("RBP", r.rbp);
    print_reg("R8", r.r8);
    print_reg("R9", r.r9);
    print_reg("R10", r.r10);
    print_reg("R11", r.r11);
    print_reg("R12", r.r12);
    print_reg("R13", r.r13);
    print_reg("R14", r.r14);
    print_reg("R15", r.r15);
    print_reg("INT", r.int_no);
    print_reg("ERR", r.err_code);

    for _ in 0..40 { (*con).panic_putc('*'); }
    (*con).panic_putc('\n');

    extern "C" { fn serial_write_panic(s: *const u8); }
    let p_msg = b"\n*** RUST EXCEPTION ***\n\0";
    serial_write_panic(p_msg.as_ptr());
    if !message.is_null() {
        serial_write_panic(message);
    }
    serial_write_panic(b"\n\0".as_ptr());
}

#[no_mangle]
pub extern "C" fn rust_log_write(buf: *const u8, len: usize) {
    let s = unsafe { core::str::from_utf8_unchecked(core::slice::from_raw_parts(buf, len)) };
    log_info(s);
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    unsafe {
        if !gcb::GCB_PTR.is_null() {
            (*gcb::GCB_PTR).panic_flag.store(1, Ordering::Release);
            
            error!("--- RUST PANIC ---");
            if let Some(location) = info.location() {
                error!("Location: {}:{}:{}", location.file(), location.line(), location.column());
            }
            error!("Message: {}", info.message());
            error!("------------------");
        }
    }

    loop {
        core::hint::spin_loop();
    }
}
