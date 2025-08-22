#pragma once
#include <stdint.h>

typedef void (*vfs_list_cb)(const char* name, int isDir);

typedef struct {
    int exists;
    int isDir;
    uint32_t size;     // for files
    uint32_t children; // for directories
} vfs_stat_t;

void vfs_init(void);
int vfs_mount_ramfs(void);

int vfs_mkdir(const char* path);
int vfs_write(const char* path, const char* data, uint32_t len); // create or truncate
int vfs_read(const char* path, char* out, uint32_t max, uint32_t* outLen);
int vfs_rm(const char* path);
int vfs_ls(const char* path, vfs_list_cb cb);
int vfs_stat(const char* path, vfs_stat_t* st);
