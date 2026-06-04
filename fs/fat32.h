#pragma once
#include <stdint.h>
#include "vfs.h"

/*
 * FAT32 Filesystem Driver
 * 
 * Implements a simplified FAT32 driver for disk storage.
 * Works with ATA/IDE drives and integrates with VFS layer.
 */

typedef struct {
    uint32_t sectors_per_cluster;
    uint32_t total_clusters;
    uint32_t reserved_sectors;
    uint32_t fat_sectors;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_cluster;
} fat32_info_t;

/* Initialize FAT32 driver */
int fat32_init(uint32_t dev_id, uint32_t lba_offset);
vfs_inode_t* fat32_get_inode(const char* path);

/* Get filesystem info */
fat32_info_t* fat32_get_info(void);

/* Read a cluster from disk */
int fat32_read_cluster(uint32_t cluster, uint8_t* buffer);

/* Write a cluster to disk */
int fat32_write_cluster(uint32_t cluster, const uint8_t* buffer);

/* Find next cluster in FAT chain */
uint32_t fat32_get_next_cluster(uint32_t cluster);

/* Allocate a new cluster */
uint32_t fat32_allocate_cluster(void);

/* Free a cluster chain */
int fat32_free_cluster_chain(uint32_t start_cluster);

/* Directory entry operations */
typedef struct {
    char name[255];
    int is_dir;
    uint32_t size;
    uint32_t start_cluster;
    uint32_t file_size;
} fat32_dir_entry_t;

/* Find file in directory cluster */
int fat32_find_in_dir(uint32_t dir_cluster, const char* name, fat32_dir_entry_t* out);

/* List directory contents */
int fat32_list_dir(uint32_t dir_cluster, void (*callback)(const fat32_dir_entry_t*));

/* Create file/directory */
int fat32_create_file(uint32_t parent_cluster, const char* name, int is_dir);

/* Delete file/directory */
int fat32_delete_file(uint32_t parent_cluster, const char* name);
