#include "fat32.h"
#include "../drivers/ata.h"
#include "../kernel/memory.h"
#include "../drivers/serial.h"
#include "bio.h"

#define SECTOR_SIZE 512
#define MAX_CLUSTERS 1048576  /* 2^20 clusters max */

static fat32_info_t g_fat32_info;
static uint32_t g_lba_offset = 0;
static uint32_t* g_fat_table = 0;
static int g_fat32_ready = 0;

/* Boot sector structure (simplified) */
typedef struct {
    uint8_t jump[3];
    uint8_t oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;  /* 0 for FAT32 */
    uint16_t total_sectors;  /* 0 for FAT32 */
    uint8_t media;
    uint16_t sectors_per_fat12_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    /* FAT32 extended info */
    uint32_t sectors_per_fat32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
} __attribute__((packed)) fat32_boot_t;

typedef struct {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t nt_res;
    uint8_t creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_raw_entry_t;

static uint32_t g_dev_id = 0;

int fat32_init(uint32_t dev_id, uint32_t lba_offset) {
    g_dev_id = dev_id;
    g_lba_offset = lba_offset;
    
    if (!ata_available()) {
        serial_writeln("[fat32] ATA not available");
        return -1;
    }
    
    /* Read boot sector */
    serial_write("[fat32] reading boot sector from dev="); serial_u64(dev_id);
    serial_write(" offset="); serial_u64(lba_offset); serial_writeln("");
    buf_t* b = bread(dev_id, lba_offset);
    if (!b || !(b->flags & B_VALID)) {
        serial_writeln("[fat32] Failed to read boot sector");
        if (b) brelse(b);
        return -2;
    }
    
    fat32_boot_t* boot = (fat32_boot_t*)b->data;
    
    serial_write("[fat32] data[0,1,2]: ");
    serial_u64(b->data[0]); serial_write(" ");
    serial_u64(b->data[1]); serial_write(" ");
    serial_u64(b->data[2]); serial_writeln("");
    
    serial_write("[fat32] bytes_per_sector: "); serial_u64(boot->bytes_per_sector); serial_writeln("");
    serial_write("[fat32] sectors_per_cluster: "); serial_u64(boot->sectors_per_cluster); serial_writeln("");
    serial_write("[fat32] root_entries: "); serial_u64(boot->root_entries); serial_writeln("");

    /* Validate boot sector */
    if (boot->bytes_per_sector != SECTOR_SIZE) {
        serial_write("[fat32] Invalid sector size: expected "); serial_u64(SECTOR_SIZE);
        serial_write(" got "); serial_u64(boot->bytes_per_sector); serial_writeln("");
        brelse(b);
        return -3;
    }
    
    if (boot->root_entries != 0) {
        serial_writeln("[fat32] Not FAT32 (has root_entries)");
        brelse(b);
        return -4;
    }
    
    /* Parse FAT32 parameters */
    g_fat32_info.sectors_per_cluster = boot->sectors_per_cluster;
    g_fat32_info.reserved_sectors = boot->reserved_sectors;
    g_fat32_info.fat_sectors = boot->sectors_per_fat32;
    g_fat32_info.fat_start_lba = lba_offset + boot->reserved_sectors;
    g_fat32_info.data_start_lba = g_fat32_info.fat_start_lba + 
                                   (boot->num_fats * boot->sectors_per_fat32);
    g_fat32_info.root_cluster = boot->root_cluster;
    g_fat32_info.total_clusters = boot->total_sectors_32 / boot->sectors_per_cluster;
    
    /* Load FAT into memory (first FAT only for now) */
    uint32_t fat_size = boot->sectors_per_fat32 * SECTOR_SIZE;
    uint32_t sectors_per_fat32 = boot->sectors_per_fat32;
    brelse(b);

    g_fat_table = (uint32_t*)kmalloc(fat_size);
    if (!g_fat_table) {
        serial_writeln("[fat32] Failed to allocate FAT table");
        return -5;
    }
    
    /* Read all FAT sectors */
    uint8_t* fat_buf = (uint8_t*)g_fat_table;
    for (uint32_t i = 0; i < sectors_per_fat32; i++) {
        uint32_t lba = g_fat32_info.fat_start_lba + i;
        buf_t* fb = bread(g_dev_id, lba);
        if (!fb || !(fb->flags & B_VALID)) {
            serial_writeln("[fat32] Failed to read FAT sector");
            if (fb) brelse(fb);
            return -6;
        }
        for (int j = 0; j < SECTOR_SIZE; j++) {
            fat_buf[i * SECTOR_SIZE + j] = fb->data[j];
        }
        brelse(fb);
    }
    
    g_fat32_ready = 1;
    serial_writeln("[fat32] Filesystem initialized successfully");
    return 0;
}

fat32_info_t* fat32_get_info(void) {
    return g_fat32_ready ? &g_fat32_info : 0;
}

int fat32_read_cluster(uint32_t cluster, uint8_t* buffer) {
    if (!g_fat32_ready || cluster >= g_fat32_info.total_clusters) {
        return -1;
    }
    
    uint32_t lba = g_fat32_info.data_start_lba + 
                   ((cluster - 2) * g_fat32_info.sectors_per_cluster);
    
    /* Read all sectors in cluster */
    for (uint32_t i = 0; i < g_fat32_info.sectors_per_cluster; i++) {
        buf_t* b = bread(g_dev_id, lba + i);
        if (!b || !(b->flags & B_VALID)) {
            if (b) brelse(b);
            return -2;
        }
        for (int j = 0; j < SECTOR_SIZE; j++) {
            buffer[i * SECTOR_SIZE + j] = b->data[j];
        }
        brelse(b);
    }
    
    return 0;
}

int fat32_write_cluster(uint32_t cluster, const uint8_t* buffer) {
    if (!g_fat32_ready || cluster >= g_fat32_info.total_clusters) {
        return -1;
    }
    
    uint32_t lba = g_fat32_info.data_start_lba + 
                   ((cluster - 2) * g_fat32_info.sectors_per_cluster);
    
    /* Write all sectors in cluster */
    for (uint32_t i = 0; i < g_fat32_info.sectors_per_cluster; i++) {
        buf_t* b = bread(g_dev_id, lba + i);
        if (!b) return -2;
        for (int j = 0; j < SECTOR_SIZE; j++) {
            b->data[j] = buffer[i * SECTOR_SIZE + j];
        }
        bwrite(b);
        brelse(b);
    }
    
    return 0;
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    if (!g_fat32_ready || cluster >= g_fat32_info.total_clusters) {
        return 0xFFFFFFFF;
    }
    
    uint32_t next = g_fat_table[cluster] & 0x0FFFFFFF;  /* Mask EOC bits */
    
    if (next >= 0xFFFFFFF8) {
        return 0xFFFFFFFF;  /* End of chain */
    }
    
    return next;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fat32_ready) return 0;
    
    /* Simple linear search for free cluster */
    for (uint32_t i = 2; i < g_fat32_info.total_clusters; i++) {
        if (g_fat_table[i] == 0) {
            g_fat_table[i] = 0xFFFFFFF8;  /* Mark as EOF */
            return i;
        }
    }
    
    return 0;  /* No free clusters */
}

int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!g_fat32_ready) return -1;
    
    uint32_t cluster = start_cluster;
    while (cluster < 0xFFFFFFF8) {
        uint32_t next = fat32_get_next_cluster(cluster);
        g_fat_table[cluster] = 0;  /* Mark as free */
        
        if (next == 0xFFFFFFFF) break;
        cluster = next;
    }
    
    return 0;
}

int fat32_list_dir(uint32_t dir_cluster, void (*callback)(const fat32_dir_entry_t*)) {
    if (!g_fat32_ready || !callback) return -1;
    
    uint8_t* dir_data = (uint8_t*)kmalloc(g_fat32_info.sectors_per_cluster * SECTOR_SIZE);
    uint32_t cluster = dir_cluster;
    
    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(cluster, dir_data) < 0) break;
        
        fat32_raw_entry_t* entries = (fat32_raw_entry_t*)dir_data;
        for (uint32_t i = 0; i < (g_fat32_info.sectors_per_cluster * SECTOR_SIZE) / sizeof(fat32_raw_entry_t); i++) {
            if (entries[i].name[0] == 0) { // End of entries
                kfree(dir_data);
                return 0;
            }
            if (entries[i].name[0] == 0xE5) continue; // Deleted
            if (entries[i].attributes == 0x0F) continue; // Long name
            
            fat32_dir_entry_t out;
            for(int k=0; k<11; k++) out.name[k] = entries[i].name[k];
            out.name[11] = 0;
            out.is_dir = (entries[i].attributes & 0x10) != 0;
            out.start_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
            out.file_size = entries[i].file_size;
            
            callback(&out);
        }
        
        cluster = fat32_get_next_cluster(cluster);
    }
    
    kfree(dir_data);
    return 0;
}

int fat32_find_in_dir(uint32_t dir_cluster, const char* name, fat32_dir_entry_t* out) {
    if (!g_fat32_ready || !out) return -1;
    
    uint8_t* dir_data = (uint8_t*)kmalloc(g_fat32_info.sectors_per_cluster * SECTOR_SIZE);
    uint32_t cluster = dir_cluster;
    
    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(cluster, dir_data) < 0) break;
        
        fat32_raw_entry_t* entries = (fat32_raw_entry_t*)dir_data;
        for (uint32_t i = 0; i < (g_fat32_info.sectors_per_cluster * SECTOR_SIZE) / sizeof(fat32_raw_entry_t); i++) {
            if (entries[i].name[0] == 0) break;
            if (entries[i].name[0] == 0xE5) continue;
            if (entries[i].attributes == 0x0F) continue;
            
            int match = 1;
            for(int k=0; k<11; k++) {
                if (k < 8) {
                    if (name[k] && entries[i].name[k] != name[k]) { match = 0; break; }
                }
            }
            
            if (match) {
                for(int k=0; k<11; k++) out->name[k] = entries[i].name[k];
                out->name[11] = 0;
                out->is_dir = (entries[i].attributes & 0x10) != 0;
                out->start_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                out->file_size = entries[i].file_size;
                kfree(dir_data);
                return 0;
            }
        }
        cluster = fat32_get_next_cluster(cluster);
    }
    
    kfree(dir_data);
    return -1;
}

int fat32_create_file(uint32_t parent_cluster, const char* name, int is_dir) {
    if (!g_fat32_ready) return -1;
    
    /* Simplified: placeholder for now */
    return 0;
}

int fat32_delete_file(uint32_t parent_cluster, const char* name) {
    if (!g_fat32_ready) return -1;
    
    /* Simplified: placeholder for now */
    return 0;
}

vfs_inode_t* fat32_get_inode(const char* path) {
    (void)path;
    return 0;
}
