#include "fat32.h"
#include "../drivers/ata.h"
#include "../kernel/memory.h"
#include "../drivers/serial.h"

#define SECTOR_SIZE 512
#define MAX_CLUSTERS 1048576  /* 2^20 clusters max */

typedef struct {
    uint8_t buffer[SECTOR_SIZE];
    uint32_t lba;
    int dirty;
} sector_cache_t;

static fat32_info_t g_fat32_info;
static sector_cache_t g_sector_cache;
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

int fat32_init(uint32_t lba_offset) {
    g_lba_offset = lba_offset;
    g_sector_cache.lba = (uint32_t)-1;
    g_sector_cache.dirty = 0;
    
    if (!ata_available()) {
        serial_writeln("[fat32] ATA not available");
        return -1;
    }
    
    /* Read boot sector */
    if (ata_pio_read28(lba_offset, g_sector_cache.buffer) < 0) {
        serial_writeln("[fat32] Failed to read boot sector");
        return -2;
    }
    
    fat32_boot_t* boot = (fat32_boot_t*)g_sector_cache.buffer;
    
    /* Validate boot sector */
    if (boot->bytes_per_sector != SECTOR_SIZE) {
        serial_writeln("[fat32] Invalid sector size");
        return -3;
    }
    
    if (boot->root_entries != 0) {
        serial_writeln("[fat32] Not FAT32 (has root_entries)");
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
    g_fat_table = (uint32_t*)kmalloc(fat_size);
    if (!g_fat_table) {
        serial_writeln("[fat32] Failed to allocate FAT table");
        return -5;
    }
    
    /* Read all FAT sectors */
    uint8_t* fat_buf = (uint8_t*)g_fat_table;
    for (uint32_t i = 0; i < boot->sectors_per_fat32; i++) {
        uint32_t lba = g_fat32_info.fat_start_lba + i;
        if (ata_pio_read28(lba, fat_buf + (i * SECTOR_SIZE)) < 0) {
            serial_writeln("[fat32] Failed to read FAT sector");
            return -6;
        }
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
        if (ata_pio_read28(lba + i, buffer + (i * SECTOR_SIZE)) < 0) {
            return -2;
        }
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
        if (ata_pio_write28(lba + i, buffer + (i * SECTOR_SIZE)) < 0) {
            return -2;
        }
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

int fat32_find_in_dir(uint32_t dir_cluster, const char* name, fat32_dir_entry_t* out) {
    if (!g_fat32_ready || !out) return -1;
    
    uint8_t* dir_data = (uint8_t*)kmalloc(
        g_fat32_info.sectors_per_cluster * SECTOR_SIZE);
    if (!dir_data) return -2;
    
    uint32_t cluster = dir_cluster;
    
    while (cluster < 0xFFFFFFF8) {
        if (fat32_read_cluster(cluster, dir_data) < 0) {
            kfree(dir_data);
            return -3;
        }
        
        /* Simplified: just scan first cluster */
        /* A real implementation would follow cluster chain */
        
        cluster = fat32_get_next_cluster(cluster);
    }
    
    kfree(dir_data);
    return -1;  /* Not found */
}

int fat32_list_dir(uint32_t dir_cluster, void (*callback)(const fat32_dir_entry_t*)) {
    if (!g_fat32_ready || !callback) return -1;
    
    /* Simplified: just placeholder for now */
    return 0;
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
