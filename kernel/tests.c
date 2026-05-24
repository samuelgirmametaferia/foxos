#include "tests.h"
#include "serial.h"
#include "memory.h"
#include "console.h"

/* Simple allocation stress test: allocate and free various sizes */
void run_alloc_stress(void) {
    serial_writeln("[tests] alloc stress start");

    /* test uc_alloc for several sizes */
    uchandle_t a = uc_alloc(1024 * 1024 * 4ULL); /* 4MB */
    if (a) serial_writeln("[tests] uc_alloc 4MB ok"); else serial_writeln("[tests] uc_alloc 4MB failed");

    uchandle_t b = uc_alloc(16 * 1024 * 1024ULL); /* 16MB */
    if (b) serial_writeln("[tests] uc_alloc 16MB ok"); else serial_writeln("[tests] uc_alloc 16MB failed");

    /* allocate many small kmallocs */
    void* ptrs[128];
    for (int i = 0; i < 128; ++i) {
        ptrs[i] = kmalloc(1024 + (i & 15));
        if (!ptrs[i]) { serial_writeln("[tests] kmalloc failed"); break; }
    }
    serial_writeln("[tests] kmalloc batch allocated");

    /* free uc_allocs */
    if (a) uc_free(a);
    if (b) uc_free(b);
    serial_writeln("[tests] uc_alloc freed");

    for (int i = 0; i < 128; ++i) if (ptrs[i]) kfree(ptrs[i]);
    serial_writeln("[tests] kmalloc batch freed");

    /* check pmm counts */
    char buf[64];
    /* local u64->dec */
    {
        uint64_t v = pmm_total_pages(); int n=0; char tmp[32]; if (v==0) { buf[n++]='0'; buf[n]=0; } else { int t=0; while(v){ tmp[t++]= '0' + (v%10); v/=10; } while(t--) buf[n++]=tmp[t]; buf[n]=0; }
        serial_write("[tests] total pages: "); serial_writeln(buf);
    }
    {
        uint64_t v = pmm_free_pages(); int n=0; char tmp[32]; if (v==0) { buf[n++]='0'; buf[n]=0; } else { int t=0; while(v){ tmp[t++]= '0' + (v%10); v/=10; } while(t--) buf[n++]=tmp[t]; buf[n]=0; }
        serial_write("[tests] free pages: "); serial_writeln(buf);
    }

    serial_writeln("[tests] alloc stress done");
}

void run_boot_self_tests(void) {
    serial_writeln("[tests] boot self-tests start");
    /* Simple sanity: allocate and free a contiguous block */
    paddr_t a = pmm_alloc_contiguous_pages(8);
    if (a) {
        serial_writeln("[tests] pmm_alloc_contiguous_pages(8) ok");
        pmm_free_contiguous_pages(a, 8);
        serial_writeln("[tests] pmm_free_contiguous_pages(8) ok");
    } else {
        serial_writeln("[tests] pmm_alloc_contiguous_pages(8) failed");
    }

    serial_writeln("[tests] boot self-tests done");
}
