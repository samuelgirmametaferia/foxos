#pragma once
#include <stdint.h>
#include "memory.h"

typedef struct ramfs_node {
    char name[32];
    uint8_t isDir;
    struct ramfs_node* parent;
    struct ramfs_node* firstChild;
    struct ramfs_node* nextSibling;
    uchandle_t data;     // for files: unified chunk handle
    uint64_t size;       // file size in bytes
} ramfs_node_t;

void ramfs_init(void);
ramfs_node_t* ramfs_root(void);
ramfs_node_t* ramfs_find(const char* path);
ramfs_node_t* ramfs_mkdir(const char* path);
vfs_inode_t* ramfs_get_inode(const char* path);
int ramfs_write(const char* path, uint64_t offset, const char* data, uint64_t len);
int ramfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen);
int ramfs_rm(const char* path);
int ramfs_ls(const char* path, void (*cb)(const char*, int));
int ramfs_stat(const char* path, int* isDir, uint64_t* size, uint64_t* children);
