#![no_std]

extern crate alloc;

mod allocator;
pub mod gcb;

use core::panic::PanicInfo;
use core::sync::atomic::Ordering;

#[no_mangle]
pub unsafe extern "C" fn rust_init(gcb_ptr: *mut gcb::Gcb) {
    if !gcb_ptr.is_null() && (*gcb_ptr).magic == gcb::GCB_MAGIC {
        gcb::GCB_PTR = gcb_ptr;
    } else {
        loop {
            core::hint::spin_loop();
        }
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe {
        if !gcb::GCB_PTR.is_null() {
            (*gcb::GCB_PTR).panic_flag.store(1, Ordering::Release);
        }
    }

    loop {
        core::hint::spin_loop();
    }
}
