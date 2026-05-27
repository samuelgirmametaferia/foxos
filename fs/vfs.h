#pragma once
#include <stdint.h>

typedef struct {
    int exists;
    int isDir;
    uint64_t size;     // for files
    uint64_t children; // for directories
} vfs_stat_t;

typedef void (*vfs_list_cb)(const char* name, int isDir);

struct vfs_inode;

typedef struct {
    int (*read)(struct vfs_inode* inode, uint64_t offset, char* buf, uint64_t len, uint64_t* outLen);
    int (*write)(struct vfs_inode* inode, uint64_t offset, const char* buf, uint64_t len);
    int (*stat)(struct vfs_inode* inode, vfs_stat_t* st);
    int (*mkdir)(struct vfs_inode* inode, const char* name);
    int (*rm)(struct vfs_inode* inode, const char* name);
    int (*ls)(struct vfs_inode* inode, vfs_list_cb cb);
} vfs_ops_t;

typedef struct vfs_inode {
    uint64_t inode_num;
    uint64_t size;
    int type; // 1: file, 2: dir
    int refcnt;
    void* private_data; // FS-specific data
    vfs_ops_t* ops;
} vfs_inode_t;

// Dentry structure
typedef struct vfs_dentry {
    char name[64];
    vfs_inode_t* inode;
    struct vfs_dentry* parent;
    struct vfs_dentry* hash_next;
} vfs_dentry_t;

vfs_inode_t* vfs_inode_alloc(void);
void vfs_inode_free(vfs_inode_t* inode);

void vfs_init(void);
int vfs_mount_ramfs(void);
int vfs_mount_foxfs(uint32_t dev_id);
int vfs_mount_fat32(uint32_t dev_id);

int vfs_mkdir(const char* path);
int vfs_write(const char* path, uint64_t offset, const char* data, uint64_t len); // create or truncate
int vfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen);
int vfs_rm(const char* path);
int vfs_ls(const char* path, vfs_list_cb cb);
int vfs_stat(const char* path, vfs_stat_t* st);

// VFS Dentry Cache API
vfs_dentry_t* vfs_dcache_lookup(vfs_dentry_t* parent, const char* name);
void vfs_dcache_add(vfs_dentry_t* parent, const char* name, vfs_inode_t* inode);

// Standardized File Handles API
int sys_open(const char* path, int flags, int mode);
int sys_read(int fd, void* buf, uint64_t count);
int sys_write(int fd, const void* buf, uint64_t count);
int sys_lseek(int fd, int64_t offset, int whence);
int sys_close(int fd);
int sys_ioctl(int fd, int cmd, void* arg);
void* sys_mmap(int fd, uint64_t length, uint64_t offset);
