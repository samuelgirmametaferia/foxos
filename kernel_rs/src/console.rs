use core::fmt::{self, Write};
use core::sync::atomic::{AtomicBool, Ordering};

extern "C" {
    fn rust_interrupts_save() -> u64;
    fn rust_interrupts_restore(rflags: u64);
}

pub struct Spinlock {
    locked: AtomicBool,
}

impl Spinlock {
    pub const fn new() -> Self {
        Self { locked: AtomicBool::new(false) }
    }
    pub fn lock(&self) -> u64 {
        let rflags = unsafe { rust_interrupts_save() };
        while self.locked.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
            core::hint::spin_loop();
        }
        rflags
    }
    pub fn unlock(&self, rflags: u64) {
        self.locked.store(false, Ordering::Release);
        unsafe { rust_interrupts_restore(rflags); }
    }
}

pub struct Console {
    cx: u32,
    cy: u32,
    cols: u32,
    rows: u32,
    color: u8,
    lock: Spinlock,
}

pub static mut CONSOLE: Console = Console {
    cx: 0,
    cy: 0,
    cols: 80,
    rows: 25,
    color: 0x0F,
    lock: Spinlock::new(),
};

extern "C" {
    fn console_putc_at(x: u32, y: u32, c: u8);
    fn console_clear_at(x: u32, y: u32);
    fn console_set_color_raw(fg: u8, bg: u8);
    fn console_newline_raw();
    fn gpu_get_width() -> u32;
    fn gpu_get_height() -> u32;
}

pub fn init() {
    unsafe {
        let w = gpu_get_width();
        let h = gpu_get_height();
        if w > 0 && h > 0 {
            CONSOLE.cols = w / 8;
            CONSOLE.rows = h / 8;
        }
        CONSOLE.cx = 0;
        CONSOLE.cy = 0;
    }
}

impl Console {
    pub fn putc(&mut self, c: char) {
        let rflags = self.lock.lock();
        self._putc_unlocked(c);
        self.lock.unlock(rflags);
    }

    pub fn panic_putc(&mut self, c: char) {
        // NO LOCKS - used during crash
        self._putc_unlocked(c);
    }

    fn _putc_unlocked(&mut self, c: char) {
        // Echo to serial for debugging and verifier support
        unsafe {
            extern "C" { fn serial_putc(c: u8); }
            serial_putc(c as u8);
        }

        if c == '\n' {
            self.cx = 0;
            self.cy += 1;
            unsafe { console_newline_raw(); }
        } else if c == '\r' {
            self.cx = 0;
        } else if c == '\t' {
            self.cx = (self.cx + 8) & !(8 - 1);
        } else if c == '\x08' { // Backspace
            if self.cx > 0 {
                self.cx -= 1;
                unsafe { console_clear_at(self.cx, self.cy); }
            }
        } else {
            let mut printable = c;
            if (c as u32) < 32 || (c as u32) >= 127 {
                printable = '?'; // Better than '.' for non-printables
            }
            unsafe { console_putc_at(self.cx, self.cy, printable as u8); }
            self.cx += 1;
        }

        if self.cx >= self.cols {
            self.cx = 0;
            self.cy += 1;
            unsafe { console_newline_raw(); }
        }

        if self.cy >= self.rows {
            // Scroll logic is usually in C's console_newline_raw, 
            // but we keep track of current row.
            self.cy = self.rows - 1;
        }
    }
    
    pub fn set_color(&mut self, fg: u8, bg: u8) {
        let rflags = self.lock.lock();
        self.color = (bg << 4) | (fg & 0x0F);
        unsafe { console_set_color_raw(fg, bg); }
        self.lock.unlock(rflags);
    }
}

impl Write for Console {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let rflags = self.lock.lock();
        for c in s.chars() {
            self._putc_unlocked(c);
        }
        self.lock.unlock(rflags);
        Ok(())
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_console_putc(c: u8) {
    (*(&raw mut CONSOLE)).putc(c as char);
}

#[no_mangle]
pub unsafe extern "C" fn rust_console_write(s: *const u8, len: usize) {
    let s = core::slice::from_raw_parts(s, len);
    let mut con = &raw mut CONSOLE;
    let rflags = (*con).lock.lock();
    for &b in s {
        (*con)._putc_unlocked(b as char);
    }
    (*con).lock.unlock(rflags);
}
