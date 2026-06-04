#include "apic.h"
#include "serial.h"
#include "io.h"
#include "../common/lib.h"

/* Local APIC base address (typically 0xFEE00000) */
static uint64_t apic_base = 0xFEE00000;
static int apic_available = 0;

/* Check if APIC is available via CPUID */
static int check_apic_available(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* CPUID: EAX=1, check EDX bit 9 for APIC */
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    return (edx & (1 << 9)) != 0;
}

/* Get APIC base address from MSR */
static void get_apic_base(void) {
    uint32_t low, high;
    
    /* Read IA32_APIC_BASE MSR (0x1B) */
    __asm__ __volatile__(
        "mov $0x1B, %%rcx\n\t"
        "rdmsr"
        : "=a"(low), "=d"(high)
        :
        : "rcx"
    );
    
    apic_base = ((uint64_t)high << 32) | (low & 0xFFFFF000);
}

/* Write to MSR */
static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    
    __asm__ __volatile__(
        "mov %0, %%rcx\n\t"
        "mov %1, %%rax\n\t"
        "mov %2, %%rdx\n\t"
        "wrmsr"
        :
        : "r"((uint64_t)msr), "r"((uint64_t)low), "r"((uint64_t)high)
        : "rcx", "rax", "rdx"
    );
}

/* Enable APIC via MSR */
static void enable_apic_via_msr(void) {
    uint32_t low, high;
    
    /* Read IA32_APIC_BASE MSR (0x1B) */
    __asm__ __volatile__(
        "mov $0x1B, %%rcx\n\t"
        "rdmsr"
        : "=a"(low), "=d"(high)
        :
        : "rcx"
    );
    
    /* Set bit 11 (APIC Enable) */
    low |= (1 << 11);
    
    /* Write back */
    wrmsr(0x1B, ((uint64_t)high << 32) | low);
}

uint32_t apic_read(uint32_t reg) {
    if (!apic_available) return 0;
    uint32_t* addr = (uint32_t*)(apic_base + reg);
    return *addr;
}

void apic_write(uint32_t reg, uint32_t value) {
    if (!apic_available) return;
    uint32_t* addr = (uint32_t*)(apic_base + reg);
    *addr = value;
}

void apic_init(void) {
    if (!check_apic_available()) {
        serial_writeln("[apic] APIC not available on this processor");
        return;
    }
    
    apic_available = 1;
    
    /* Get APIC base address */
    get_apic_base();
    
    serial_write("[apic] APIC base: 0x");
    char buf[32];
    u64_to_hex(apic_base, buf);
    serial_writeln(buf);
    
    /* Enable APIC */
    enable_apic_via_msr();
    
    /* Set TPR to 0 to allow all interrupts */
    apic_write(0x080, 0);

    /* Set spurious interrupt vector register */
    uint32_t sivr = apic_read(APIC_SIVR_REG);
    sivr |= 0x1FF;  /* Vector 255 + APIC enable */
    apic_write(APIC_SIVR_REG, sivr);
    
    /* Enable Virtual Wire Mode (route PIC interrupts via LINT0) */
    apic_write(0x350, 0x700); // LVT LINT0: ExtINT delivery mode
    
    /* Initialize IOAPIC for legacy IRQs */
    for (int i = 0; i < 24; i++) {
        // Unmask and set vector (removed 0x10000 mask bit)
        uint32_t low = (32 + i);
        volatile uint32_t* base = (uint32_t*)0xFEC00000;
        base[0] = 0x10 + i * 2; base[4] = low;
        base[0] = 0x10 + i * 2 + 1; base[4] = 0;
    }
    
    // Route IRQ 1 (Keyboard) to Vector 33 (0x21)
    {
        volatile uint32_t* base = (uint32_t*)0xFEC00000;
        base[0] = 0x10 + 1 * 2; base[4] = 33; // Vector 33
        base[0] = 0x10 + 1 * 2 + 1; base[4] = 0; // Destination BSP
    }
    // Route IRQ 0 (PIT) to Vector 32 (0x20)
    {
        volatile uint32_t* base = (uint32_t*)0xFEC00000;
        base[0] = 0x10 + 0 * 2; base[4] = 32; // Vector 32
        base[0] = 0x10 + 0 * 2 + 1; base[4] = 0; // Destination BSP
    }

    /* Get APIC ID */
    uint32_t id = apic_read(0x20); // Local APIC ID Register
    serial_write("[apic] Local APIC ID: ");
    char idb[16]; u32_to_dec(id >> 24, idb);
    serial_writeln(idb);
    serial_writeln("[apic] APIC initialized");
}

uint32_t apic_get_id(void) {
    if (!apic_available) return 0;
    uint32_t id_reg = apic_read(APIC_ID_REG);
    return (id_reg >> 24) & 0xFF;
}

int apic_is_available(void) {
    return apic_available;
}

void apic_send_ipi(uint32_t dest_apic_id, uint32_t vector) {
    if (!apic_available) return;
    
    /* Set ICR high register (destination) */
    uint32_t icr_high = (dest_apic_id & 0xFF) << 24;
    apic_write(APIC_ICR_HIGH_REG, icr_high);
    
    /* Set ICR low register (delivery mode and vector) */
    uint32_t icr_low = (IPI_FIXED << 8) | (vector & 0xFF);
    apic_write(APIC_ICR_LOW_REG, icr_low);
}

void apic_send_ipi_all(uint32_t vector) {
    if (!apic_available) return;
    
    uint32_t icr_low = (IPI_FIXED << 8) | (IPI_DEST_ALL << 18) | (vector & 0xFF);
    apic_write(APIC_ICR_LOW_REG, icr_low);
}

void apic_send_ipi_all_except_self(uint32_t vector) {
    if (!apic_available) return;
    
    uint32_t icr_low = (IPI_FIXED << 8) | (IPI_DEST_ALL_EXCEPT << 18) | (vector & 0xFF);
    apic_write(APIC_ICR_LOW_REG, icr_low);
}

void apic_set_error_vector(uint8_t vector) {
    if (!apic_available) return;
    uint32_t lvt_error = (vector & 0xFF) | (1 << 16);  /* Set enable bit */
    apic_write(APIC_LVT_ERROR_REG, lvt_error);
}

void apic_timer_init(uint32_t frequency, uint8_t vector) {
    if (!apic_available) return;
    
    /* Set divisor to 16 */
    apic_write(APIC_TIMER_DIV_REG, 0x03);
    
    /* Mask timer during setup */
    apic_write(APIC_LVT_TIMER_REG, 0x10000);
    
    /* Calculate rough ticks based on frequency 
       Assume APIC bus speed is ~1GHz, 100Hz = 10M ticks. */
    uint32_t ticks = 10000000;
    if (frequency > 0) {
        ticks = 1000000000 / 16 / frequency; 
    }
    
    /* Set periodic mode and vector */
    apic_write(APIC_LVT_TIMER_REG, (APIC_TIMER_PERIODIC << 17) | (vector & 0xFF));
    
    /* Set initial count */
    apic_write(APIC_TIMER_INIT_REG, ticks);
}
