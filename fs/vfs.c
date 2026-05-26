#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "ramfs.h"
#include "fat32.h"

#define VFS_MODE_RAMFS 1
#define VFS_MODE_FAT32 2

static int g_vfs_mode = VFS_MODE_RAMFS;

void vfs_init(void) {
    ramfs_init();
    
    /* Try to mount FAT32 if available */
    if (fat32_init(0) == 0) {
        g_vfs_mode = VFS_MODE_FAT32;
    }
}

int vfs_mount_ramfs(void) {
    g_vfs_mode = VFS_MODE_RAMFS;
    return 0;
}

int vfs_mount_fat32(uint32_t lba_offset) {
    if (fat32_init(lba_offset) == 0) {
        g_vfs_mode = VFS_MODE_FAT32;
        return 0;
    }
    return -1;
}

int vfs_mkdir(const char* path) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        return fat32_create_file(2, path, 1);  /* 2 is root cluster */
    }
    return ramfs_mkdir(path) ? 0 : -1;
}

int vfs_write(const char* path, const char* data, uint64_t len) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        /* FAT32 write not yet fully implemented */
        return -1;
    }
    return ramfs_write(path, data, len);
}

int vfs_read(const char* path, char* out, uint64_t max, uint64_t* outLen) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        /* FAT32 read not yet fully implemented */
        return -1;
    }
    return ramfs_read(path, out, max, outLen);
}

int vfs_rm(const char* path) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        return fat32_delete_file(2, path);  /* 2 is root cluster */
    }
    return ramfs_rm(path);
}

int vfs_ls(const char* path, vfs_list_cb cb) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        return fat32_list_dir(2, NULL);  /* 2 is root cluster */
    }
    return ramfs_ls(path, cb);
}

int vfs_stat(const char* path, vfs_stat_t* st) {
    if (g_vfs_mode == VFS_MODE_FAT32) {
        /* FAT32 stat not yet fully implemented */
        if (st) {
            st->exists = 0;
            st->isDir = 0;
            st->size = 0;
            st->children = 0;
        }
        return -1;
    }
    
    int isd = 0;
    uint64_t sz = 0, ch = 0;
    int r = ramfs_stat(path, &isd, &sz, &ch);
    if (st) {
        st->exists = (r == 0);
        st->isDir = isd;
        st->size = sz;
        st->children = ch;
    }
    return r;
}
