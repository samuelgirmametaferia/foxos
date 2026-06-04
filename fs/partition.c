#include "partition.h"
#include "bio.h"
#include "../common/lib.h"
#include "../kernel/memory.h"
#include "../drivers/serial.h"

typedef struct {
    uint8_t bootable;
    uint8_t start_head;
    uint8_t start_sec;
    uint8_t start_cyl;
    uint8_t type;
    uint8_t end_head;
    uint8_t end_sec;
    uint8_t end_cyl;
    uint32_t lba_start;
    uint32_t lba_count;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint32_t parent_dev_id;
    uint32_t lba_offset;
    uint32_t lba_count;
} part_info_t;

static int part_read(struct blk_device* self, uint64_t lba, void* buf, uint32_t count) {
    part_info_t* pi = (part_info_t*)self->private_data;
    if (lba + count > pi->lba_count) return -1;
    
    blk_device_t* parent = blk_get_device(pi->parent_dev_id);
    if (!parent) return -2;
    
    return parent->read(parent, pi->lba_offset + lba, buf, count);
}

static int part_write(struct blk_device* self, uint64_t lba, const void* buf, uint32_t count) {
    part_info_t* pi = (part_info_t*)self->private_data;
    if (lba + count > pi->lba_count) return -1;
    
    blk_device_t* parent = blk_get_device(pi->parent_dev_id);
    if (!parent) return -2;
    
    return parent->write(parent, pi->lba_offset + lba, buf, count);
}

static blk_device_t partition_devs[4];
static part_info_t partition_infos[4];

void partition_init(void) {
    // Read MBR from dev 0 (ATA Master)
    buf_t* b = bread(0, 0);
    if (!b) return;
    
    mbr_partition_t* parts = (mbr_partition_t*)(b->data + 0x1BE);
    int gpt_detected = 0;
    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0xEE) {
            gpt_detected = 1;
            break;
        }
    }

    if (gpt_detected) {
        serial_writeln("[part] GPT detected, parsing...");
        brelse(b);
        b = bread(0, 1); // GPT Header
        if (!b) return;
        
        if (memcmp(b->data, "EFI PART", 8) != 0) {
            serial_writeln("[part] GPT signature mismatch");
            brelse(b);
            return;
        }
        
        uint64_t entry_lba = *(uint64_t*)(b->data + 72);
        uint32_t entry_count = *(uint32_t*)(b->data + 80);
        uint32_t entry_size = *(uint32_t*)(b->data + 84);
        brelse(b);
        
        serial_write("[part] GPT entries at LBA "); serial_u64(entry_lba);
        serial_write(" count="); serial_u64(entry_count);
        serial_writeln("");
        
        // Just read the first entry LBA (usually has 4 entries per 512B sector)
        b = bread(0, entry_lba);
        if (!b) return;
        
        for (uint32_t i = 0; i < entry_count && i < 4; i++) {
            uint8_t* entry = b->data + (i * entry_size);
            // Check partition type GUID (non-zero)
            int has_type = 0;
            for (int j = 0; j < 16; j++) if (entry[j] != 0) has_type = 1;
            if (!has_type) continue;
            
            uint64_t start_lba = *(uint64_t*)(entry + 32);
            uint64_t end_lba = *(uint64_t*)(entry + 40);
            
            serial_write("[part] GPT partition "); serial_u64(i);
            serial_write(" start="); serial_u64(start_lba);
            serial_write(" end="); serial_u64(end_lba);
            serial_writeln("");
            
            partition_infos[i].parent_dev_id = 0;
            partition_infos[i].lba_offset = (uint32_t)start_lba;
            partition_infos[i].lba_count = (uint32_t)(end_lba - start_lba + 1);
            
            partition_devs[i].read = part_read;
            partition_devs[i].write = part_write;
            partition_devs[i].private_data = &partition_infos[i];
            partition_devs[i].parent = blk_get_device(0);
            
            blk_register_device(i + 1, &partition_devs[i]);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (parts[i].type != 0) {
                serial_write("[part] Found partition "); serial_u64(i);
                serial_write(" type="); serial_u64(parts[i].type);
                serial_write(" start="); serial_u64(parts[i].lba_start);
                serial_write(" count="); serial_u64(parts[i].lba_count);
                serial_writeln("");
                
                partition_infos[i].parent_dev_id = 0;
                partition_infos[i].lba_offset = parts[i].lba_start;
                partition_infos[i].lba_count = parts[i].lba_count;
                
                partition_devs[i].read = part_read;
                partition_devs[i].write = part_write;
                partition_devs[i].private_data = &partition_infos[i];
                partition_devs[i].parent = blk_get_device(0);
                
                blk_register_device(i + 1, &partition_devs[i]);
            }
        }
    }
    
    brelse(b);
}
