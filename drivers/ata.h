#pragma once
#include <stdint.h>

// Very small ATA PIO driver for primary master (LBA28, 512-byte sectors)

int ata_init(void);           // returns 0 on success, <0 if not present
int ata_available(void);      // 1 if a drive seems present
int ata_pio_read28(uint32_t lba, void* buf);   // read 1 sector
int ata_pio_write28(uint32_t lba, const void* buf); // write 1 sector

int ata_pio_read48(uint64_t lba, void* buf, uint16_t sector_count);
int ata_pio_write48(uint64_t lba, const void* buf, uint16_t sector_count);

struct blk_device;
int ata_dma_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t sector_count);
int ata_dma_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t sector_count);
