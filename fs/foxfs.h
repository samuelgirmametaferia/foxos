#pragma once
#include <stdint.h>
#include "bio.h"
#include "vfs.h"

// 1. Extent representation
typedef struct {
    uint32_t logical_block;   // Starting block relative to file start
    uint32_t physical_block;  // Starting block on physical disk
    uint32_t block_count;      // Length of contiguous block run
} foxfs_extent_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_table_start;
    uint32_t bitmap_start;
    uint32_t journal_start;
    uint32_t data_start;
} foxfs_superblock_t;

typedef struct {
    uint32_t inode_num;
    uint64_t size;
    uint32_t type;     // 1 for file, 2 for dir
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    foxfs_extent_t extents[8]; // 8 * 12 = 96 bytes
    uint32_t extent_tree_root;
    uint8_t reserved[108]; // Pad to 256 bytes
} foxfs_inode_t;

typedef struct {
    uint32_t magic;
    uint32_t transaction_id;
    uint32_t block_count;
    uint32_t blocks[125]; // Fits in 512B
} foxfs_journal_block_t;

typedef struct {
    char name[60];
    uint32_t inode_num;
} foxfs_dir_entry_t; // 64 bytes, 64 per 4KB block

void foxfs_init(void);
int foxfs_format(uint32_t dev_id, uint32_t lba_count);
int foxfs_mount(uint32_t dev_id);
int foxfs_defragment(const char* path);

// Delayed allocation flush
void foxfs_flush_delalloc(void);

// Journaling
void foxfs_journal_start(void);
void foxfs_journal_commit(void);

int foxfs_mkdir(const char* path);
vfs_inode_t* foxfs_get_inode(const char* path);
int foxfs_write(const char* path, uint64_t offset, const char* data, uint64_t len);
int foxfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen);
int foxfs_rm(const char* path);
int foxfs_ls(const char* path, vfs_list_cb cb);
int foxfs_stat(const char* path, vfs_stat_t* st);
