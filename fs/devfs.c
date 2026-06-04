#include "devfs.h"
#include "../kernel/memory.h"
#include "../kernel/spinlock.h"
#include "../drivers/serial.h"

#define MAX_DEVICES 32

typedef struct {
    char name[32];
    devfs_ops_t* ops;
    int active;
} devfs_node_t;

static devfs_node_t g_devices[MAX_DEVICES];
static spinlock_t g_devfs_lock = SPINLOCK_INIT;

static int devfs_op_read(vfs_inode_t* inode, uint64_t offset, char* buf, uint64_t len, uint64_t* outLen) {
    devfs_node_t* dev = (devfs_node_t*)inode->private_data;
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    (void)offset;
    return dev->ops->read(buf, len, outLen);
}

static int devfs_op_write(vfs_inode_t* inode, uint64_t offset, const char* buf, uint64_t len) {
    devfs_node_t* dev = (devfs_node_t*)inode->private_data;
    if (!dev || !dev->ops || !dev->ops->write) return -1;
    (void)offset;
    return dev->ops->write(buf, len);
}

static int devfs_op_stat(vfs_inode_t* inode, vfs_stat_t* st) {
    (void)inode;
    if (st) {
        st->exists = 1;
        st->isDir = 0;
        st->size = 0;
        st->children = 0;
    }
    return 0;
}

static vfs_ops_t devfs_vfs_ops = {
    .read = devfs_op_read,
    .write = devfs_op_write,
    .stat = devfs_op_stat,
};

vfs_inode_t* devfs_get_inode(const char* name) {
    serial_write("[devfs] get_inode: "); serial_write(name); serial_writeln("");
    spinlock_acquire(&g_devfs_lock);
    serial_writeln("[devfs] lock acquired");
    devfs_node_t* dev = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].active) {
            int match = 1, k = 0;
            while (name[k] && g_devices[i].name[k]) {
                if (name[k] != g_devices[i].name[k]) { match = 0; break; }
                k++;
            }
            if (match && !name[k] && !g_devices[i].name[k]) {
                dev = &g_devices[i];
                break;
            }
        }
    }
    spinlock_release(&g_devfs_lock);
    serial_writeln("[devfs] lock released");
    
    if (!dev) {
        serial_writeln("[devfs] device not found");
        return 0;
    }
    
    serial_writeln("[devfs] device found, allocating inode");
    vfs_inode_t* inode = vfs_inode_alloc();
    if (!inode) {
        serial_writeln("[devfs] vfs_inode_alloc failed");
        return 0;
    }
    serial_write("[devfs] inode allocated: "); serial_u64((uint64_t)inode); serial_writeln("");
    inode->inode_num = (uint64_t)dev;
    inode->size = 0;
    inode->type = 1; // file
    inode->private_data = dev;
    inode->ops = &devfs_vfs_ops;
    return inode;
}

void devfs_init(void) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        g_devices[i].active = 0;
    }
}

static int streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static void strncpy_safe(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int devfs_register(const char* name, devfs_ops_t* ops) {
    spinlock_acquire(&g_devfs_lock);
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!g_devices[i].active) {
            strncpy_safe(g_devices[i].name, name, 32);
            g_devices[i].ops = ops;
            g_devices[i].active = 1;
            spinlock_release(&g_devfs_lock);
            return 0;
        }
    }
    spinlock_release(&g_devfs_lock);
    return -1;
}

static devfs_node_t* find_dev(const char* name) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].active && streq(g_devices[i].name, name)) {
            return &g_devices[i];
        }
    }
    return 0;
}

int devfs_stat(const char* name, vfs_stat_t* st) {
    spinlock_acquire(&g_devfs_lock);
    devfs_node_t* dev = find_dev(name);
    spinlock_release(&g_devfs_lock);
    if (!dev) return -1;
    if (st) {
        st->exists = 1;
        st->isDir = 0;
        st->size = 0;
        st->children = 0;
    }
    return 0;
}

int devfs_read(const char* name, uint64_t offset, char* out, uint64_t max, uint64_t* outLen) {
    spinlock_acquire(&g_devfs_lock);
    devfs_node_t* dev = find_dev(name);
    spinlock_release(&g_devfs_lock);
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    (void)offset; // devfs typically ignores offset for stream devices
    return dev->ops->read(out, max, outLen);
}

int devfs_write(const char* name, uint64_t offset, const char* data, uint64_t len) {
    spinlock_acquire(&g_devfs_lock);
    devfs_node_t* dev = find_dev(name);
    spinlock_release(&g_devfs_lock);
    if (!dev || !dev->ops || !dev->ops->write) return -1;
    (void)offset;
    return dev->ops->write(data, len);
}

int devfs_ioctl(const char* name, int cmd, void* arg) {
    spinlock_acquire(&g_devfs_lock);
    devfs_node_t* dev = find_dev(name);
    spinlock_release(&g_devfs_lock);
    if (!dev || !dev->ops || !dev->ops->ioctl) return -1;
    return dev->ops->ioctl(cmd, arg);
}

void* devfs_mmap(const char* name, uint64_t length, uint64_t offset) {
    spinlock_acquire(&g_devfs_lock);
    devfs_node_t* dev = find_dev(name);
    spinlock_release(&g_devfs_lock);
    if (!dev || !dev->ops || !dev->ops->mmap) return (void*)-1;
    return dev->ops->mmap(length, offset);
}
