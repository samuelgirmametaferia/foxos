use core::ffi::c_void;
use core::sync::atomic::{AtomicU32, AtomicU64};

pub const GCB_MAGIC: u64 = u64::from_le_bytes(*b"FOX0GCB1");

#[repr(C, align(64))]
pub struct Gcb {
    pub magic: u64,
    pub panic_flag: AtomicU64,
    pub scheduler_state: *mut c_void,
    pub cpu_count: u32,
    pub log_head: AtomicU32,
    pub log_tail: AtomicU32,
    pub _pad0: [u8; 24],
    pub current_log: [u8; 1008],
}

pub static mut GCB_PTR: *mut Gcb = core::ptr::null_mut();
