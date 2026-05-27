#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "devfs.h"
#include "ramfs.h"
#include "fat32.h"
#include "foxfs.h"
#include "../kernel/rwlock.h"
#include "../kernel/memory.h"
#include "../kernel/process.h"
#include "../kernel/spinlock.h"
#include "../drivers/serial.h"

static int is_devfs_path(const char* path);

#define VFS_MODE_RAMFS 1
#define VFS_MODE_FAT32 2
#define VFS_MODE_FOXFS 3

static int g_vfs_mode = VFS_MODE_RAMFS;

// --- Dentry Cache Implementation ---
#define DCACHE_HASH_SIZE 256
static vfs_dentry_t* dcache_hash[DCACHE_HASH_SIZE];
static rwlock_t dcache_lock = RWLOCK_INIT;

static uint32_t dcache_hash_func(vfs_dentry_t* parent, const char* name) {
    uint32_t h = (uint32_t)((uintptr_t)parent);
    while (*name) { h = h * 31 + *name++; }
    return h % DCACHE_HASH_SIZE;
}

vfs_inode_t* vfs_inode_alloc(void) {
    vfs_inode_t* inode = (vfs_inode_t*)kmalloc(sizeof(vfs_inode_t));
    if (inode) {
        inode->inode_num = 0;
        inode->size = 0;
        inode->type = 0;
        inode->refcnt = 1;
        inode->private_data = NULL;
        inode->ops = NULL;
    }
    return inode;
}

void vfs_inode_free(vfs_inode_t* inode) {
    if (!inode) return;
    inode->refcnt--;
    if (inode->refcnt == 0) {
        // Here we might need to call FS-specific cleanup if needed, 
        // but for now just free the memory.
        kfree(inode);
    }
}

vfs_dentry_t* vfs_dcache_lookup(vfs_dentry_t* parent, const char* name) {
    uint32_t h = dcache_hash_func(parent, name);
    rwlock_acquire_read(&dcache_lock);
    vfs_dentry_t* d = dcache_hash[h];
    while (d) {
        if (d->parent == parent) {
            int match = 1, i = 0;
            while(name[i] && d->name[i]) {
                if (name[i] != d->name[i]) { match = 0; break; }
                i++;
            }
            if (match && !name[i] && !d->name[i]) {
                rwlock_release_read(&dcache_lock);
                return d;
            }
        }
        d = d->hash_next;
    }
    rwlock_release_read(&dcache_lock);
    return 0;
}

void vfs_dcache_add(vfs_dentry_t* parent, const char* name, vfs_inode_t* inode) {
    if (vfs_dcache_lookup(parent, name)) return; 
    
    uint32_t h = dcache_hash_func(parent, name);
    vfs_dentry_t* d = (vfs_dentry_t*)kmalloc(sizeof(vfs_dentry_t));
    if (!d) return;
    int i = 0; while(name[i] && i < 63) { d->name[i] = name[i]; i++; } d->name[i] = 0;
    d->parent = parent;
    d->inode = inode;
    if (inode) inode->refcnt++;
    
    rwlock_acquire_write(&dcache_lock);
    d->hash_next = dcache_hash[h];
    dcache_hash[h] = d;
    rwlock_release_write(&dcache_lock);
}

// --- File Descriptor Table Implementation ---
#define MAX_FDS 256

typedef struct {
    int active;
    vfs_inode_t* inode;
    int64_t offset;
    int flags;
    char path[128];
} vfs_file_t;

static vfs_file_t fd_table[MAX_FDS];
static spinlock_t fd_lock = SPINLOCK_INIT;

static vfs_inode_t* vfs_resolve_path(const char* path) {
    if (is_devfs_path(path)) {
        return devfs_get_inode(path + 5);
    }
    if (g_vfs_mode == VFS_MODE_FOXFS) {
        return foxfs_get_inode(path);
    }
    return ramfs_get_inode(path);
}

int sys_open(const char* path, int flags, int mode) {
    vfs_inode_t* inode = vfs_resolve_path(path);
    if (!inode) {
        if (flags & 1) { // O_CREAT
            if (vfs_write(path, 0, "", 0) != 0) return -1;
            inode = vfs_resolve_path(path);
        } else {
            return -1;
        }
    }
    
    if (!inode) return -1;

    spinlock_acquire(&fd_lock);
    int fd = -1;
    for(int i = 0; i < MAX_FDS; i++) {
        if (!fd_table[i].active) {
            fd = i;
            fd_table[i].active = 1;
            break;
        }
    }
    spinlock_release(&fd_lock);
    
    if (fd < 0) {
        vfs_inode_free(inode);
        return -1;
    }
    
    fd_table[fd].inode = inode;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = flags;
    int k=0; while(path[k] && k < 127) { fd_table[fd].path[k] = path[k]; k++; } fd_table[fd].path[k]=0;
    return fd;
}

int sys_read(int fd, void* buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return -1;
    vfs_inode_t* inode = fd_table[fd].inode;
    if (!inode || !inode->ops || !inode->ops->read) return -1;
    
    uint64_t outLen = 0;
    int r = inode->ops->read(inode, fd_table[fd].offset, (char*)buf, count, &outLen);
    if (r == 0) fd_table[fd].offset += outLen;
    return r == 0 ? (int)outLen : -1;
}

int sys_write(int fd, const void* buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return -1;
    vfs_inode_t* inode = fd_table[fd].inode;
    if (!inode || !inode->ops || !inode->ops->write) return -1;
    
    int r = inode->ops->write(inode, fd_table[fd].offset, (const char*)buf, count);
    if (r == 0) fd_table[fd].offset += count;
    return r == 0 ? (int)count : -1;
}

int sys_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return -1;
    vfs_inode_t* inode = fd_table[fd].inode;
    if (whence == 0) fd_table[fd].offset = offset;
    else if (whence == 1) fd_table[fd].offset += offset;
    else if (whence == 2) {
        fd_table[fd].offset = inode->size + offset;
    }
    return (int)fd_table[fd].offset;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return -1;
    spinlock_acquire(&fd_lock);
    vfs_inode_free(fd_table[fd].inode);
    fd_table[fd].active = 0;
    spinlock_release(&fd_lock);
    return 0;
}

int sys_ioctl(int fd, int cmd, void* arg) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return -1;
    if (is_devfs_path(fd_table[fd].path)) {
        return devfs_ioctl(fd_table[fd].path + 5, cmd, arg);
    }
    return -1;
}

void* sys_mmap(int fd, uint64_t length, uint64_t offset) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].active) return (void*)-1;
    void* paddr = (void*)-1;
    if (is_devfs_path(fd_table[fd].path)) {
        paddr = devfs_mmap(fd_table[fd].path + 5, length, offset);
    }
    
    if (paddr != (void*)-1) {
        // Map physical address to virtual address in user space
        process_t* proc = process_get_current();
        if (proc->is_user) {
            uint64_t vaddr = (uint64_t)paddr | 0x8000000000ULL;

            /* Debug: log mmap attempt */
            serial_write("[vfs] sys_mmap: dev=");
            serial_write(fd_table[fd].path + 5);
            serial_write(" paddr="); serial_u64((uint64_t)paddr);
            serial_write(" vaddr="); serial_u64(vaddr);
            serial_write(" len="); serial_u64(length);
            serial_writeln("");

            for (uint64_t off = 0; off < length; off += PAGE_SIZE) {
                vmm_map(proc->page_directory, vaddr + off, (uint64_t)paddr + off, 0x07); // P | R/W | U
            }

            serial_writeln("[vfs] sys_mmap: mapped OK");
            return (void*)vaddr;
        } else {
            /* Kernel mapping: just return physical pointer */
            return paddr;
        }
    }

    serial_writeln("[vfs] sys_mmap: failed");
    return (void*)-1;
}

// --- Legacy Path API ---
void vfs_init(void) {
    ramfs_init();
    foxfs_init();
    devfs_init();
    
    for (int i = 0; i < DCACHE_HASH_SIZE; i++) {
        dcache_hash[i] = 0;
    }
    
    for (int i = 0; i < MAX_FDS; i++) {
        fd_table[i].active = 0;
    }
}

int vfs_mount_ramfs(void) {
    g_vfs_mode = VFS_MODE_RAMFS;
    return 0;
}

int vfs_mount_foxfs(uint32_t dev_id) {
    if (foxfs_mount(dev_id) == 0) {
        g_vfs_mode = VFS_MODE_FOXFS;
        return 0;
    }
    return -1;
}

int vfs_mount_fat32(uint32_t dev_id) {
    (void)dev_id;
#ifdef PT_LBA_START
    if (fat32_init(PT_LBA_START) == 0) {
        g_vfs_mode = VFS_MODE_FAT32;
        return 0;
    }
#else
    /* If PT_LBA_START is not defined at compile time, try default offset 0 */
    if (fat32_init(0) == 0) {
        g_vfs_mode = VFS_MODE_FAT32;
        return 0;
    }
#endif
    return -1;
}

int vfs_mkdir(const char* path) {
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_mkdir(path);
    return ramfs_mkdir(path) ? 0 : -1;
}

static int is_devfs_path(const char* path) {
    return (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/');
}

int vfs_write(const char* path, uint64_t offset, const char* data, uint64_t len) {
    if (is_devfs_path(path)) return devfs_write(path + 5, offset, data, len);
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_write(path, offset, data, len);
    return ramfs_write(path, offset, data, len);
}

int vfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen) {
    if (is_devfs_path(path)) return devfs_read(path + 5, offset, out, max, outLen);
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_read(path, offset, out, max, outLen);
    return ramfs_read(path, offset, out, max, outLen);
}

int vfs_rm(const char* path) {
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_rm(path);
    return ramfs_rm(path);
}

int vfs_ls(const char* path, vfs_list_cb cb) {
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_ls(path, cb);
    return ramfs_ls(path, cb);
}

int vfs_stat(const char* path, vfs_stat_t* st) {
    if (is_devfs_path(path)) return devfs_stat(path + 5, st);
    if (g_vfs_mode == VFS_MODE_FOXFS) return foxfs_stat(path, st);
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
