use crate::error;

pub const SYS_EXIT:    u64 = 1;
pub const SYS_WRITE:   u64 = 2;
pub const SYS_READ:    u64 = 3;
pub const SYS_SPAWN:   u64 = 4;
pub const SYS_YIELD:   u64 = 5;
pub const SYS_SLEEP:   u64 = 6;
pub const SYS_GETPID:  u64 = 7;
pub const SYS_OPEN:    u64 = 8;
pub const SYS_CLOSE:   u64 = 9;
pub const SYS_IOCTL:   u64 = 10;
pub const SYS_MMAP:    u64 = 11;
pub const SYS_EXEC:    u64 = 12;
pub const SYS_GETINFO: u64 = 13;

extern "C" {
    fn process_exit(code: u64);
    fn console_putc(c: u8);
    fn sys_write(fd: i32, buf: *const u8, count: u64) -> i64;
    fn sys_read(fd: i32, buf: *mut u8, count: u64) -> i64;
    fn keyboard_getchar() -> i32;
    fn timer_sleep(ms: u64);
    fn process_spawn(entry: extern "C" fn(), is_user: i32) -> i32;
    fn scheduler_yield();
    fn scheduler_thread_sleep(tid: i32, ms: u64);
    fn scheduler_current_thread_id() -> i32;
    fn sys_open(path: *const u8, flags: i32, mode: i32) -> i32;
    fn sys_close(fd: i32) -> i32;
    fn sys_ioctl(fd: i32, cmd: i32, arg: *mut core::ffi::c_void) -> i32;
    fn sys_mmap(fd: i32, length: u64, offset: u64) -> *mut core::ffi::c_void;
    fn sys_exec(path: *const u8, argv: *const *const u8, envp: *const *const u8) -> i32;
    fn rust_get_system_info(info: *mut crate::SysInfo) -> i32;
    fn process_get_count() -> u32;
}

#[no_mangle]
pub unsafe extern "C" fn rust_syscall_handler(
    rdi: u64, rsi: u64, rdx: u64, rcx: u64, _r8: u64, _r9: u64, sys_num: u64
) -> u64 {
    match sys_num {
        SYS_EXIT => {
            process_exit(rdi);
            0
        }
        SYS_WRITE => {
            if rsi < 0x8000000000 { return u64::MAX; }
            let fd = rdi as i32;
            let buf = rsi as *const u8;
            let count = rdx;
            if fd == 1 || fd == 2 {
                let slice = core::slice::from_raw_parts(buf, count as usize);
                for &b in slice {
                    console_putc(b);
                }
                count
            } else {
                sys_write(fd, buf, count) as u64
            }
        }
        SYS_READ => {
            if rsi < 0x8000000000 { return u64::MAX; }
            let fd = rdi as i32;
            let buf = rsi as *mut u8;
            let count = rdx;
            if fd == 0 {
                let mut read_bytes = 0;
                while read_bytes < count {
                    let ch = keyboard_getchar();
                    if ch != -1 {
                        *buf.add(read_bytes as usize) = ch as u8;
                        read_bytes += 1;
                    } else {
                        timer_sleep(10);
                    }
                }
                read_bytes
            } else {
                sys_read(fd, buf, count) as u64
            }
        }
        SYS_SPAWN => {
            process_spawn(core::mem::transmute(rdi), 1) as u64
        }
        SYS_YIELD => {
            scheduler_yield();
            0
        }
        SYS_SLEEP => {
            scheduler_thread_sleep(scheduler_current_thread_id(), rdi);
            0
        }
        SYS_GETPID => {
            0 
        }
        SYS_OPEN => {
            sys_open(rdi as *const u8, rsi as i32, rdx as i32) as u64
        }
        SYS_CLOSE => {
            sys_close(rdi as i32) as u64
        }
        SYS_IOCTL => {
            sys_ioctl(rdi as i32, rsi as i32, rdx as *mut core::ffi::c_void) as u64
        }
        SYS_MMAP => {
            sys_mmap(rdi as i32, rsi, rdx) as u64
        }
        SYS_EXEC => {
            sys_exec(rdi as *const u8, rsi as *const *const u8, rdx as *const *const u8) as u64
        }
        SYS_GETINFO => {
            if rdi < 0x8000000000 {
                return u64::MAX; // Reject kernel pointers
            }
            let info_ptr = rdi as *mut crate::SysInfo;
            let ret = rust_get_system_info(info_ptr);
            if ret == 0 {
                unsafe {
                    let info = &mut *info_ptr;
                    info.process_count = process_get_count();
                }
            }
            ret as u64
        }
        _ => {
            error!("Unknown syscall: {}", sys_num);
            u64::MAX
        }
    }
}
