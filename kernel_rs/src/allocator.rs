use core::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn kmalloc(size: usize) -> *mut u8;
    fn kfree(ptr: *mut u8);
}

pub struct FoxOSAllocator;

unsafe impl GlobalAlloc for FoxOSAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let size = layout.size().max(layout.align()).max(1);
        let ptr = kmalloc(size);
        if ptr.is_null() {
            loop {
                core::hint::spin_loop();
            }
        }
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        kfree(ptr);
    }
}

#[global_allocator]
pub static GLOBAL_ALLOCATOR: FoxOSAllocator = FoxOSAllocator;
