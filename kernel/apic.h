#pragma once
#include <stdint.h>

/*
 * APIC (Advanced Programmable Interrupt Controller) support for x86-64.
 * Handles Local APIC and inter-processor interrupts (IPIs).
 */

/* APIC Register offsets */
#define APIC_ID_REG        0x020
#define APIC_VERSION_REG   0x030
#define APIC_TPR_REG       0x080
#define APIC_APR_REG       0x090
#define APIC_PPR_REG       0x0A0
#define APIC_EOI_REG       0x0B0
#define APIC_SIVR_REG      0x0F0
#define APIC_ISR_REG       0x100
#define APIC_TMR_REG       0x180
#define APIC_IRR_REG       0x200
#define APIC_ESR_REG       0x280
#define APIC_CMCI_REG      0x2F0
#define APIC_ICR_LOW_REG   0x300
#define APIC_ICR_HIGH_REG  0x310
#define APIC_LVT_TIMER_REG 0x320
#define APIC_LVT_PERF_REG  0x340
#define APIC_LVT_LINT0_REG 0x350
#define APIC_LVT_LINT1_REG 0x360
#define APIC_LVT_ERROR_REG 0x370
#define APIC_TIMER_INIT_REG 0x380
#define APIC_TIMER_CUR_REG  0x390
#define APIC_TIMER_DIV_REG  0x3E0

/* APIC Timer modes */
#define APIC_TIMER_ONESHOT  0
#define APIC_TIMER_PERIODIC 1

/* IPI destination modes */
#define IPI_DEST_SELF       1
#define IPI_DEST_ALL        2
#define IPI_DEST_ALL_EXCEPT 3

/* IPI delivery modes */
#define IPI_FIXED      0
#define IPI_LOWEST_PRI 1
#define IPI_SMI        2
#define IPI_NMI        4
#define IPI_INIT       5
#define IPI_SIPI       6

/* Initialize Local APIC */
void apic_init(void);

/* Initialize Local APIC Timer */
void apic_timer_init(uint32_t frequency, uint8_t vector);

/* Get Local APIC ID of current CPU */
uint32_t apic_get_id(void);

/* Read APIC register */
uint32_t apic_read(uint32_t reg);

/* Write APIC register */
void apic_write(uint32_t reg, uint32_t value);

/* Send IPI to single CPU */
void apic_send_ipi(uint32_t dest_apic_id, uint32_t vector);

/* Send IPI to all CPUs */
void apic_send_ipi_all(uint32_t vector);

/* Send IPI to all CPUs except self */
void apic_send_ipi_all_except_self(uint32_t vector);

/* Check if APIC is available */
int apic_is_available(void);

/* Set APIC error vector */
void apic_set_error_vector(uint8_t vector);
