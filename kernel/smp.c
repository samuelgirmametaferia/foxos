#include "smp.h"
#include "serial.h"

static uint32_t cpu_count = 1;

static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
    if (a) *a = eax;
    if (b) *b = ebx;
    if (c) *c = ecx;
    if (d) *d = edx;
}

void smp_init(void) {
    uint32_t a=0,b=0,c=0,d=0;
    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 0x0B) {
        uint32_t max = 1;
        for (uint32_t level = 0; level < 4; ++level) {
            cpuid(0x0B, level, &a, &b, &c, &d);
            uint32_t level_type = (c >> 8) & 0xFF;
            if (level_type == 0) break;
            if (b > max) max = b;
        }
        cpu_count = max;
    } else {
        cpuid(1, 0, &a, &b, &c, &d);
        uint32_t logical = (b >> 16) & 0xFF;
        cpu_count = logical ? logical : 1;
    }
    serial_write("[smp] cpu_count: ");
    char buf[16]; int n=0; uint32_t v=cpu_count; if (v==0){ buf[n++]='0'; buf[n]=0; } else { char tmp[16]; int t=0; while(v){ tmp[t++]=(char)('0'+(v%10)); v/=10; } while(t--) buf[n++]=tmp[t]; buf[n]=0; }
    serial_writeln(buf);
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}
