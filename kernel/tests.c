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

    /* Run defragmentation tests */
    {
        serial_writeln("[tests] defrag tests start");
        const int N = 16;
        uchandle_t hs[N];
        for (int i = 0; i < N; ++i) {
            hs[i] = uc_alloc(PAGE_SIZE);
            if (!hs[i]) { serial_writeln("[tests] uc_alloc small failed"); hs[i] = 0; }
            else {
                /* write an id pattern into first 16 bytes */
                void* tmp = kmalloc(64);
                if (tmp) {
                    for (int j = 0; j < 64; ++j) ((uint8_t*)tmp)[j] = (uint8_t)(i & 0xFF);
                    uc_write(hs[i], tmp, 64);
                    kfree(tmp);
                }
            }
        }

        /* free every other handle to create fragmentation */
        for (int i = 0; i < N; i += 2) {
            if (hs[i]) { uc_free(hs[i]); hs[i] = 0; }
        }

        /* attempt defragmentation */
        int moved = uc_defrag_all();
        char mb[32]; int mbl=0; if (moved==0) { mb[mbl++]='0'; mb[mbl]=0; } else { int v=moved, t=0; char tmp[32]; while(v){ tmp[t++]=(char)('0'+(v%10)); v/=10; } while(t--) mb[mbl++]=tmp[t]; mb[mbl]=0; }
        serial_write("[tests] uc_defrag_all moved: "); serial_writeln(mb);

        /* verify content of remaining handles */
        for (int i = 1; i < N; i += 2) {
            if (!hs[i]) continue;
            uint8_t buf[64]; if (uc_read(hs[i], 0, buf, 64) == 0) {
                int ok = 1; for (int k = 0; k < 64; ++k) if (buf[k] != (uint8_t)(i & 0xFF)) { ok = 0; break; }
                if (ok) serial_writeln("[tests] defrag data ok"); else serial_writeln("[tests] defrag data MISMATCH");
            } else {
                serial_writeln("[tests] defrag read failed");
            }
        }

        /* cleanup */
        for (int i = 1; i < N; i += 2) if (hs[i]) uc_free(hs[i]);
        serial_writeln("[tests] defrag tests done");
    }
}
