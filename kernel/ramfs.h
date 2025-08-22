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
    uint32_t size;       // file size in bytes
} ramfs_node_t;

void ramfs_init(void);
ramfs_node_t* ramfs_root(void);
ramfs_node_t* ramfs_find(const char* path);
ramfs_node_t* ramfs_mkdir(const char* path);
int ramfs_write(const char* path, const char* data, uint32_t len);
int ramfs_read(const char* path, char* out, uint32_t max, uint32_t* outLen);
int ramfs_rm(const char* path);
int ramfs_ls(const char* path, void (*cb)(const char*, int));
int ramfs_stat(const char* path, int* isDir, uint32_t* size, uint32_t* children);
