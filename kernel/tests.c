#include "tests.h"
#include "serial.h"
#include "memory.h"
#include "console.h"

/* Simple allocation stress test: allocate and free various sizes */
void run_alloc_stress(void) {
    serial_writeln("[tests] alloc stress start");

    /* test uc_alloc for several sizes */
    movehandle_t a = move_alloc(1024 * 1024 * 4ULL); /* 4MB */
    if (a) serial_writeln("[tests] uc_alloc 4MB ok"); else serial_writeln("[tests] uc_alloc 4MB failed");

    movehandle_t b = move_alloc(16 * 1024 * 1024ULL); /* 16MB */
    if (b) serial_writeln("[tests] uc_alloc 16MB ok"); else serial_writeln("[tests] uc_alloc 16MB failed");

    /* allocate many small move-backed allocations */
    movehandle_t ptrs[128];
    for (int i = 0; i < 128; ++i) {
        ptrs[i] = move_kmalloc(1024 + (i & 15));
        if (!ptrs[i]) { serial_writeln("[tests] move_kmalloc failed"); break; }
    }
    serial_writeln("[tests] move_kmalloc batch allocated");

    /* free move-backed allocations and uc handles */
    if (a) move_free(a);
    if (b) move_free(b);
    serial_writeln("[tests] move allocs freed");

    for (int i = 0; i < 128; ++i) if (ptrs[i]) move_kfree(ptrs[i]);
    serial_writeln("[tests] move_kmalloc batch freed");

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
    paddr_t a = pmm_alloc_contiguous_pages(2);
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
        movehandle_t hs[N];
        for (int i = 0; i < N; ++i) {
            hs[i] = move_alloc(PAGE_SIZE);
            if (!hs[i]) { serial_writeln("[tests] uc_alloc small failed"); hs[i] = 0; }
            else {
                /* write an id pattern into first 16 bytes */
                uint8_t tmp[64];
                for (int j = 0; j < 64; ++j) tmp[j] = (uint8_t)(i & 0xFF);
                move_write(hs[i], tmp, 64);
            }
        }

        /* free every other handle to create fragmentation */
        for (int i = 0; i < N; i += 2) {
            if (hs[i]) { move_free(hs[i]); hs[i] = 0; }
        }

        /* attempt defragmentation */
        int moved = move_defrag_all();
        char mb[32]; int mbl=0; if (moved==0) { mb[mbl++]='0'; mb[mbl]=0; } else { int v=moved, t=0; char tmp[32]; while(v){ tmp[t++]=(char)('0'+(v%10)); v/=10; } while(t--) mb[mbl++]=tmp[t]; mb[mbl]=0; }
        serial_write("[tests] uc_defrag_all moved: "); serial_writeln(mb);

        /* verify content of remaining handles */
        for (int i = 1; i < N; i += 2) {
            if (!hs[i]) continue;
            uint8_t buf[64]; int rc = move_read(hs[i], 0, buf, 64);
            if (rc == 0) {
                int ok = 1; for (int k = 0; k < 64; ++k) if (buf[k] != (uint8_t)(i & 0xFF)) { ok = 0; break; }
                if (ok) serial_writeln("[tests] defrag data ok"); else serial_writeln("[tests] defrag data MISMATCH");
            } else {
                serial_write("[tests] defrag read failed code: ");
                char tmpbuf[32]; int tn=0; int v=rc; if (v<0) { serial_putc('-'); v = -v; }
                if (v==0) { tmpbuf[tn++]='0'; } else { char tmp2[32]; int t=0; while(v){ tmp2[t++]=(char)('0'+(v%10)); v/=10; } while(t--) tmpbuf[tn++]=tmp2[t]; }
                tmpbuf[tn]=0; serial_write(tmpbuf); serial_writeln("");
            }
        }

        /* cleanup */
        for (int i = 1; i < N; i += 2) if (hs[i]) move_free(hs[i]);
        serial_writeln("[tests] defrag tests done");
    }
}
