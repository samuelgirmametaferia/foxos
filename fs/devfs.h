#pragma once
#include <stdint.h>
#include "vfs.h"

typedef struct {
    int (*read)(char* out, uint64_t max, uint64_t* outLen);
    int (*write)(const char* data, uint64_t len);
    int (*ioctl)(int cmd, void* arg);
    void* (*mmap)(uint64_t length, uint64_t offset);
} devfs_ops_t;

void devfs_init(void);
int devfs_register(const char* name, devfs_ops_t* ops);
vfs_inode_t* devfs_get_inode(const char* name);

int devfs_read(const char* name, uint64_t offset, char* out, uint64_t max, uint64_t* outLen);
int devfs_write(const char* name, uint64_t offset, const char* data, uint64_t len);
int devfs_stat(const char* name, vfs_stat_t* st);
int devfs_ioctl(const char* name, int cmd, void* arg);
void* devfs_mmap(const char* name, uint64_t length, uint64_t offset);
