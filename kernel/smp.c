#include "smp.h"
#include "serial.h"
#include "apic.h"
#include "timer.h"
#include "memory.h"
#include "percpu.h"
#include "idt.h"
#include "sched.h"
#include "gdt.h"
#include "syscall.h"

// Define a 10-byte structure for the GDT pointer
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

static uint32_t cpu_count = 1;
gdt_ptr_t bsp_gdt; // Shared GDT pointer saved from the BSP

// External reference to the generated trampoline binary header
#include "../build/ap_trampoline.h"

// 64-bit C entry point for APs
void ap_kernel_entry(void);

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

static void send_init_ipi(uint32_t apic_id) {
    // ICR High: Destination APIC ID
    apic_write(0x310, (apic_id & 0xFF) << 24);
    // ICR Low: INIT delivery mode (5), level=1, assert=1 -> 0x4500
    apic_write(0x300, 0x4500);
}

static void send_startup_ipi(uint32_t apic_id, uint32_t vector) {
    // ICR High: Destination APIC ID
    apic_write(0x310, (apic_id & 0xFF) << 24);
    // ICR Low: SIPI delivery mode (6), vector -> 0x4600 | vector
    apic_write(0x300, 0x4600 | (vector & 0xFF));
}

void smp_init(void) {
    // 1. Detect CPU count via CPUID
    uint32_t a=0, b=0, c=0, d=0;
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

    serial_write("[smp] Detected logical CPU cores: ");
    char num_buf[16];
    {
        uint32_t v = cpu_count; int n = 0; char tmp[16];
        if (v == 0) { num_buf[n++] = '0'; num_buf[n] = 0; }
        else {
            int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
            while (t--) num_buf[n++] = tmp[t]; num_buf[n] = 0;
        }
    }
    serial_writeln(num_buf);

    if (cpu_count <= 1) {
        serial_writeln("[smp] Single-core CPU, skipping AP initialization");
        return;
    }

    // 2. Save BSP GDT pointer
    __asm__ __volatile__("sgdt %0" : "=m"(bsp_gdt));

    // 3. Copy AP real-mode boot trampoline to low physical memory (address 0x8000)
    serial_writeln("[smp] Copying trampoline to physical address 0x8000...");
    uint8_t* dest = (uint8_t*)0x8000;
    for (unsigned int i = 0; i < ap_trampoline_len; ++i) {
        dest[i] = ap_trampoline_code[i];
    }

    // 4. Read BSP's PML4 address from CR3
    uint64_t bsp_pml4 = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(bsp_pml4));

    // 5. Populate trampoline variables (offsets defined in ap_trampoline.asm)
    *(volatile uint64_t*)(0x8000 + 8)  = bsp_pml4;             // PML4 pointer at offset 8
    *(volatile uint64_t*)(0x8000 + 24) = (uint64_t)ap_kernel_entry; // C entry point at offset 24

    // 6. Boot Application Processors (APs) sequentially
    for (uint32_t cpu_id = 1; cpu_id < cpu_count; ++cpu_id) {
        uint32_t apic_id = cpu_id; // Assume sequential APIC IDs under standard QEMU

        serial_write("[smp] Booting AP core ");
        char id_buf[16];
        {
            uint32_t v = cpu_id; int n = 0; char tmp[16];
            if (v == 0) { id_buf[n++] = '0'; }
            else {
                int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
                while (t--) id_buf[n++] = tmp[t];
            }
            id_buf[n] = 0;
        }
        serial_write(id_buf);
        serial_write(" (APIC ID ");
        {
            uint32_t v = apic_id; int n = 0; char tmp[16];
            if (v == 0) { id_buf[n++] = '0'; }
            else {
                int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
                while (t--) id_buf[n++] = tmp[t];
            }
            id_buf[n] = 0;
        }
        serial_write(id_buf);
        serial_writeln(")...");

        // Allocate a separate 16KB stack for this AP
        uint8_t* stack = (uint8_t*)kmalloc(16384);
        if (!stack) {
            serial_writeln("[smp] ERROR: Failed to allocate stack for AP!");
            continue;
        }
        uint64_t rsp = (uint64_t)stack + 16384;
        rsp &= ~0xFUL; // Align stack to 16 bytes

        // Set AP-specific trampoline variables
        *(volatile uint64_t*)(0x8000 + 32) = cpu_id;   // CPU ID at offset 32
        *(volatile uint64_t*)(0x8000 + 40) = apic_id;   // APIC ID at offset 40
        *(volatile uint64_t*)(0x8000 + 16) = rsp;       // Stack pointer at offset 16 (triggers AP)

        // Send INIT IPI to wake up the processor
        send_init_ipi(apic_id);
        timer_sleep(10); // Wait 10ms

        // Send first SIPI pointing to real-mode entry (0x8000 corresponds to vector 0x08)
        send_startup_ipi(apic_id, 0x08);
        timer_sleep(1); // Wait 1ms

        // Send second SIPI pointing to real-mode entry
        send_startup_ipi(apic_id, 0x08);
        timer_sleep(1); // Wait 1ms

        // Wait for the AP to boot up and clear ap_stack_ptr (acknowledgment)
        int timeout = 1000;
        while (*(volatile uint64_t*)(0x8000 + 16) != 0 && --timeout > 0) {
            timer_sleep(1);
        }

        if (timeout == 0) {
            serial_writeln("[smp] ERROR: AP boot timed out!");
        } else {
            serial_writeln("[smp] AP booted successfully!");
        }
    }
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}

int smp_get_core_id(void) {
    uint32_t b=0;
    cpuid(1, 0, 0, &b, 0, 0);
    return (int)((b >> 24) & 0xFF); // Initial APIC ID
}

// 64-bit C entry point called by booted APs
void ap_kernel_entry(void) {
    // 1. Disable interrupts until we are ready
    __asm__ __volatile__("cli");

    // 2. Load the GDT and TSS for this AP
    // (This replaces the old bsp_gdt load)
    gdt_init_ap();

    // 2b. Initialize syscall MSRs for this AP
    syscall_init_ap();

    // 4. Load the system IDT
    idt_load_for_ap();

    // 5. Read CPU ID and APIC ID assigned by the BSP from the low memory area
    uint32_t cpu_id = (uint32_t)*(volatile uint64_t*)(0x8000 + 32);
    uint32_t apic_id = (uint32_t)*(volatile uint64_t*)(0x8000 + 40);

    // 6. Initialize per-CPU area for this core
    percpu_init_ap(cpu_id, apic_id);

    // 7. Enable Local APIC on this core (Vector 255 + APIC enable)
    uint32_t sivr = apic_read(0x0F0); // SIVR is at register 0x0F0
    sivr |= 0x1FF;
    apic_write(0x0F0, sivr);

    serial_write("[smp] AP core online! CPU ID: ");
    char num_buf[16];
    {
        uint32_t v = cpu_id; int n = 0; char tmp[16];
        if (v == 0) { num_buf[n++] = '0'; }
        else {
            int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
            while (t--) num_buf[n++] = tmp[t];
        }
        num_buf[n] = 0;
    }
    serial_writeln(num_buf);

    // 8. Start scheduling threads on this AP core
    apic_timer_init(100, 34);
    scheduler_ap_start();
}
