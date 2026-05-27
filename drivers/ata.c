#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "ata.h"
#include "quiesce.h"
#include "timer.h"
#include "serial.h"
#include "io_wait.h"
#include "../fs/bio.h"

// Forward declarations for blk_device functions
static int ata_pio_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t count);
static int ata_pio_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t count);
int ata_dma_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t sector_count);
int ata_dma_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t sector_count);

static blk_device_t ata_blk_dev = {
    .read = NULL,
    .write = NULL
};

// Primary bus IO ports
#define ATA_IO_BASE   0x1F0
#define ATA_CTRL_BASE 0x3F6

#define REG_DATA   (ATA_IO_BASE + 0)
#define REG_ERROR  (ATA_IO_BASE + 1)
#define REG_SECCNT (ATA_IO_BASE + 2)
#define REG_LBA0   (ATA_IO_BASE + 3)
#define REG_LBA1   (ATA_IO_BASE + 4)
#define REG_LBA2   (ATA_IO_BASE + 5)
#define REG_HDDEV  (ATA_IO_BASE + 6)
#define REG_STATUS (ATA_IO_BASE + 7)
#define REG_CMD    (ATA_IO_BASE + 7)

#define REG_CTRL   (ATA_CTRL_BASE)

#define STATUS_ERR  (1<<0)
#define STATUS_DRQ  (1<<3)
#define STATUS_SRV  (1<<4)
#define STATUS_DF   (1<<5)
#define STATUS_RDY  (1<<6)
#define STATUS_BSY  (1<<7)

static int g_ata_present = 0;
static volatile int g_ata_quiesced = 0;

static wait_queue_t ata_wait_queue;
static uint16_t bmdma_base = 0;

typedef struct {
    uint32_t phys_addr;
    uint16_t byte_count;
    uint16_t flags; // 0x8000 for EOT
} __attribute__((packed)) prdt_entry_t;

static prdt_entry_t prdt[16] __attribute__((aligned(4)));

#include "idt.h"

extern void idt_unmask_irq(uint8_t irq);

void ata_irq_handler(registers_t* regs) {
    if (bmdma_base) {
        uint8_t status = inb(bmdma_base + 2);
        outb(bmdma_base + 2, status | 0x04); // clear interrupt
    }
    inb(REG_STATUS); // Clear IDE interrupt
    io_wait_wake_all(&ata_wait_queue);
}

static void ata_quiesce_cb(int enter) {
    g_ata_quiesced = enter ? 1 : 0;
    serial_write("[ata] quiesce "); serial_writeln(enter?"enter":"exit");
}

static void ata_delay400ns(){ inb(REG_STATUS); inb(REG_STATUS); inb(REG_STATUS); inb(REG_STATUS); }

static uint8_t status_wait(uint8_t mask, uint8_t match){
    uint8_t s;
    int iters = 1000000;
    do { s = inb(REG_STATUS); } while ((s & mask) != match && --iters > 0);
    if (iters == 0) serial_writeln("[ata] status_wait timeout!");
    return s;
}

int ata_init(void){
    // Select master, LBA
    outb(REG_HDDEV, 0xE0);
    ata_delay400ns();
    // Zero sector count and LBA regs and issue IDENTIFY
    outb(REG_SECCNT, 0);
    outb(REG_LBA0, 0);
    outb(REG_LBA1, 0);
    outb(REG_LBA2, 0);
    outb(REG_CMD, 0xEC);
    uint8_t s = inb(REG_STATUS);
    if (s == 0) { g_ata_present = 0; return -1; }
    // Wait while BSY
    s = status_wait(STATUS_BSY, 0);
    if (s & STATUS_ERR) { g_ata_present = 0; return -2; }
    if (!(s & STATUS_DRQ)) { g_ata_present = 0; return -3; }
    // Read 256 words identify data and discard
    for (int i=0;i<256;i++){ (void)inw(REG_DATA); }
    g_ata_present = 1;
    io_wait_queue_init(&ata_wait_queue);
    extern void ata_irq_handler(registers_t* regs);
    idt_register_handler(46, ata_irq_handler);
    idt_unmask_irq(14);
    
    // Find PCI Bus Master
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | 0x80000000);
            outl(0xCF8, address);
            uint32_t vendor_device = inl(0xCFC);
            if ((vendor_device & 0xFFFF) == 0xFFFF) continue;
            
            outl(0xCF8, address | 8);
            uint32_t class_sub = inl(0xCFC);
            if ((class_sub >> 16) == 0x0101) { // IDE Controller
                outl(0xCF8, address | 0x20); // BAR4
                uint32_t bar4 = inl(0xCFC);
                if (bar4 & 1) { // I/O Space
                    bmdma_base = bar4 & 0xFFFC;
                    outl(bmdma_base + 4, (uint32_t)(uintptr_t)prdt);
                    serial_writeln("[ata] Found IDE Bus Master DMA");
                    break;
                }
            }
        }
        if (bmdma_base) break;
    }
    
    // If DMA was found, use DMA. Otherwise, fall back to PIO.
    if (bmdma_base) {
        serial_writeln("[ata] Using DMA for block device 0");
        ata_blk_dev.read = ata_dma_read;
        ata_blk_dev.write = ata_dma_write;
    } else {
        serial_writeln("[ata] Using PIO for block device 0 (DMA not available)");
        ata_blk_dev.read = ata_pio_read; 
        ata_blk_dev.write = ata_pio_write;
    }

    blk_register_device(0, &ata_blk_dev);
    /* register quiesce callback so ATA I/O pauses during compaction */
    quiesce_register(ata_quiesce_cb);

    serial_writeln("[ata] Initialization complete. Primary Master registered as block device 0.");
    return 0;
}

int ata_available(void){ return g_ata_present; }

static void insw(uint16_t port, void* addr, int count){ uint16_t* p=(uint16_t*)addr; while(count--) *p++ = inw(port); }
static void outsw(uint16_t port, const void* addr, int count){ const uint16_t* p=(const uint16_t*)addr; while(count--) outw(port, *p++); }

int ata_pio_read28(uint32_t lba, void* buf){
    if (!g_ata_present) return -1;
    status_wait(STATUS_BSY, 0);
    // 28-bit LBA, 1 sector
    outb(REG_HDDEV, 0xE0 | ((lba>>24)&0x0F));
    outb(REG_SECCNT, 1);
    outb(REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(REG_LBA1, (uint8_t)((lba>>8) & 0xFF));
    outb(REG_LBA2, (uint8_t)((lba>>16) & 0xFF));
    outb(REG_CMD, 0x20); // READ SECTORS
    /* respect quiesce state */
    while (g_ata_quiesced) { timer_sleep(1); }
    uint8_t s = status_wait(STATUS_BSY, 0);
    if (s & (STATUS_ERR|STATUS_DF)) return -2;
    if (!(s & STATUS_DRQ)) return -3;
    insw(REG_DATA, buf, 256);
    return 0;
}

int ata_pio_read48(uint64_t lba, void* buf, uint16_t sector_count) {
    if (!g_ata_present) return -1;
    status_wait(STATUS_BSY, 0);
    outb(REG_HDDEV, 0x40); // LBA
    outb(REG_SECCNT, (uint8_t)(sector_count >> 8));
    outb(REG_LBA0, (uint8_t)(lba >> 24));
    outb(REG_LBA1, (uint8_t)(lba >> 32));
    outb(REG_LBA2, (uint8_t)(lba >> 40));
    outb(REG_SECCNT, (uint8_t)sector_count);
    outb(REG_LBA0, (uint8_t)lba);
    outb(REG_LBA1, (uint8_t)(lba >> 8));
    outb(REG_LBA2, (uint8_t)(lba >> 16));
    outb(REG_CMD, 0x24); // READ SECTORS EXT

    while (g_ata_quiesced) { timer_sleep(1); }
    for (int i = 0; i < sector_count; i++) {
        uint8_t s = status_wait(STATUS_BSY, 0);
        if (s & (STATUS_ERR|STATUS_DF)) return -2;
        if (!(s & STATUS_DRQ)) return -3;
        insw(REG_DATA, (uint8_t*)buf + (i * 512), 256);
    }
    return 0;
}

int ata_pio_write28(uint32_t lba, const void* buf){
    if (!g_ata_present) return -1;
    status_wait(STATUS_BSY, 0);
    outb(REG_HDDEV, 0xE0 | ((lba>>24)&0x0F));
    outb(REG_SECCNT, 1);
    outb(REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(REG_LBA1, (uint8_t)((lba>>8) & 0xFF));
    outb(REG_LBA2, (uint8_t)((lba>>16) & 0xFF));
    outb(REG_CMD, 0x30); // WRITE SECTORS
    /* respect quiesce state */
    while (g_ata_quiesced) { timer_sleep(1); }
    uint8_t s = status_wait(STATUS_BSY, 0);
    if (s & (STATUS_ERR|STATUS_DF)) return -2;
    if (!(s & STATUS_DRQ)) return -3;
    outsw(REG_DATA, buf, 256);
    // flush cache
    outb(REG_CMD, 0xE7);
    status_wait(STATUS_BSY, 0);
    return 0;
}

int ata_pio_write48(uint64_t lba, const void* buf, uint16_t sector_count) {
    if (!g_ata_present) return -1;
    status_wait(STATUS_BSY, 0);
    outb(REG_HDDEV, 0x40); // LBA
    outb(REG_SECCNT, (uint8_t)(sector_count >> 8));
    outb(REG_LBA0, (uint8_t)(lba >> 24));
    outb(REG_LBA1, (uint8_t)(lba >> 32));
    outb(REG_LBA2, (uint8_t)(lba >> 40));
    outb(REG_SECCNT, (uint8_t)sector_count);
    outb(REG_LBA0, (uint8_t)lba);
    outb(REG_LBA1, (uint8_t)(lba >> 8));
    outb(REG_LBA2, (uint8_t)(lba >> 16));
    outb(REG_CMD, 0x34); // WRITE SECTORS EXT

    while (g_ata_quiesced) { timer_sleep(1); }
    for (int i = 0; i < sector_count; i++) {
        uint8_t s = status_wait(STATUS_BSY, 0);
        if (s & (STATUS_ERR|STATUS_DF)) return -2;
        if (!(s & STATUS_DRQ)) return -3;
        outsw(REG_DATA, (const uint8_t*)buf + (i * 512), 256);
    }
    // flush cache
    outb(REG_CMD, 0xEA); // FLUSH CACHE EXT
    status_wait(STATUS_BSY, 0);
    return 0;
}

extern void pmm_pin_page(uint64_t);
extern void pmm_unpin_page(uint64_t);

static int ata_dma_transfer(uint64_t lba, void* buf, uint32_t sector_count, int is_write) {
    if (!g_ata_present || !bmdma_base) return -1;
    if (sector_count == 0) return 0;
    
    uint64_t addr = (uint64_t)(uintptr_t)buf;
    uint32_t byte_count = sector_count * 512;
    if (byte_count > 65536) return -3;
    
    for(uint32_t offset = 0; offset < byte_count; offset += 4096) {
        pmm_pin_page(addr + offset);
    }
    
    prdt[0].phys_addr = (uint32_t)addr;
    prdt[0].byte_count = (byte_count == 65536) ? 0 : byte_count;
    prdt[0].flags = 0x8000; // EOT
    
    outb(bmdma_base + 0, 0x00); // Stop
    outb(bmdma_base + 2, inb(bmdma_base + 2) | 0x04 | 0x02); // Clear Interrupt and Error flags
    outb(bmdma_base + 0, is_write ? 0x00 : 0x08); // Write=0x00, Read=0x08
    
    int use_lba48 = (lba > 0x0FFFFFFF) || (sector_count > 256);

    if (use_lba48) {
        outb(REG_HDDEV, 0x40);
        outb(REG_SECCNT, (uint8_t)(sector_count >> 8));
        outb(REG_LBA0, (uint8_t)(lba >> 24));
        outb(REG_LBA1, (uint8_t)(lba >> 32));
        outb(REG_LBA2, (uint8_t)(lba >> 40));
        outb(REG_SECCNT, (uint8_t)sector_count);
        outb(REG_LBA0, (uint8_t)lba);
        outb(REG_LBA1, (uint8_t)(lba >> 8));
        outb(REG_LBA2, (uint8_t)(lba >> 16));
        outb(REG_CMD, is_write ? 0x35 : 0x25); // WRITE DMA EXT : READ DMA EXT
    } else {
        outb(REG_HDDEV, 0xE0 | ((lba >> 24) & 0x0F));
        outb(REG_SECCNT, (uint8_t)sector_count);
        outb(REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(REG_CMD, is_write ? 0xCA : 0xC8); // WRITE DMA : READ DMA
    }
    
    outb(bmdma_base + 0, (is_write ? 0x00 : 0x08) | 0x01); // Start
    
    // Block thread
    io_wait_on_queue(&ata_wait_queue);
    
    uint8_t bm_status = inb(bmdma_base + 2);
    outb(bmdma_base + 0, 0x00); // Stop
    outb(bmdma_base + 2, bm_status | 0x04 | 0x02); // Clear flags
    
    for(uint32_t offset = 0; offset < byte_count; offset += 4096) {
        pmm_unpin_page(addr + offset);
    }
    
    if (bm_status & 0x02) return -2; // Error
    
    return 0;
}

int ata_dma_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t sector_count) {
    (void)self;
    return ata_dma_transfer(lba, buf, sector_count, 0);
}

int ata_dma_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t sector_count) {
    (void)self;
    return ata_dma_transfer(lba, (void*)buf, sector_count, 1);
}

static int ata_pio_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t count) {
    (void)self;
    for (uint32_t i = 0; i < count; i++) {
        if (ata_pio_read28((uint32_t)(lba + i), (uint8_t*)buf + (i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ata_pio_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t count) {
    (void)self;
    for (uint32_t i = 0; i < count; i++) {
        if (ata_pio_write28((uint32_t)(lba + i), (uint8_t*)buf + (i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}
