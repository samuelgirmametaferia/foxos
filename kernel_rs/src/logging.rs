use core::fmt::{self, Write};
use crate::gcb;
use core::sync::atomic::Ordering;

pub enum LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
}

impl LogLevel {
    pub(crate) fn as_str(&self) -> &'static str {
        match self {
            LogLevel::Debug => "DEBUG",
            LogLevel::Info  => "INFO ",
            LogLevel::Warn  => "WARN ",
            LogLevel::Error => "ERROR",
            LogLevel::Fatal => "FATAL",
        }
    }
}

pub struct Logger;

impl Write for Logger {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for b in s.as_bytes() {
            log_putc(*b);
        }
        Ok(())
    }
}

fn log_putc(c: u8) {
    unsafe {
        if !gcb::GCB_PTR.is_null() {
            let gcb = &*gcb::GCB_PTR;
            let head = gcb.log_head.load(Ordering::Relaxed);
            let next = (head + 1) % 1008;
            
            let log_ptr = gcb.current_log.as_ptr() as *mut u8;
            core::ptr::write_volatile(log_ptr.add(head as usize), c);
            
            gcb.log_head.store(next, Ordering::Release);
        }
        
        // Also echo to serial for convenience
        extern "C" {
            fn serial_putc(c: u8);
        }
        serial_putc(c);
    }
}

#[macro_export]
macro_rules! log {
    ($level:expr, $($arg:tt)*) => {{
        use core::fmt::Write;
        let mut logger = $crate::logging::Logger;
        let _ = write!(logger, "[{}] ", $level.as_str());
        let _ = write!(logger, $($arg)*);
        let _ = write!(logger, "\n");
    }};
}

#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => ($crate::log!($crate::logging::LogLevel::Info, $($arg)*));
}

#[macro_export]
macro_rules! warn {
    ($($arg:tt)*) => ($crate::log!($crate::logging::LogLevel::Warn, $($arg)*));
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => ($crate::log!($crate::logging::LogLevel::Error, $($arg)*));
}
