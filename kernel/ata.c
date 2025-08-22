#include <stdint.h>
#include "io.h"
#include "ata.h"

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

static void ata_delay400ns(){ inb(REG_STATUS); inb(REG_STATUS); inb(REG_STATUS); inb(REG_STATUS); }

static uint8_t status_wait(uint8_t mask, uint8_t match){
    uint8_t s;
    int iters = 100000;
    do { s = inb(REG_STATUS); } while ((s & mask) != match && --iters > 0);
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
    return 0;
}

int ata_available(void){ return g_ata_present; }

static void insw(uint16_t port, void* addr, int count){ uint16_t* p=(uint16_t*)addr; while(count--) *p++ = inw(port); }
static void outsw(uint16_t port, const void* addr, int count){ const uint16_t* p=(const uint16_t*)addr; while(count--) outw(port, *p++); }

int ata_pio_read28(uint32_t lba, void* buf){
    if (!g_ata_present) return -1;
    // 28-bit LBA, 1 sector
    outb(REG_HDDEV, 0xE0 | ((lba>>24)&0x0F));
    outb(REG_SECCNT, 1);
    outb(REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(REG_LBA1, (uint8_t)((lba>>8) & 0xFF));
    outb(REG_LBA2, (uint8_t)((lba>>16) & 0xFF));
    outb(REG_CMD, 0x20); // READ SECTORS
    uint8_t s = status_wait(STATUS_BSY, 0);
    if (s & (STATUS_ERR|STATUS_DF)) return -2;
    if (!(s & STATUS_DRQ)) return -3;
    insw(REG_DATA, buf, 256);
    return 0;
}

int ata_pio_write28(uint32_t lba, const void* buf){
    if (!g_ata_present) return -1;
    outb(REG_HDDEV, 0xE0 | ((lba>>24)&0x0F));
    outb(REG_SECCNT, 1);
    outb(REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(REG_LBA1, (uint8_t)((lba>>8) & 0xFF));
    outb(REG_LBA2, (uint8_t)((lba>>16) & 0xFF));
    outb(REG_CMD, 0x30); // WRITE SECTORS
    uint8_t s = status_wait(STATUS_BSY, 0);
    if (s & (STATUS_ERR|STATUS_DF)) return -2;
    if (!(s & STATUS_DRQ)) return -3;
    outsw(REG_DATA, buf, 256);
    // flush cache
    outb(REG_CMD, 0xE7);
    status_wait(STATUS_BSY, 0);
    return 0;
}
