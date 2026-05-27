#include "foxfs.h"
#include <stddef.h>
#include "../kernel/memory.h"
#include "../drivers/serial.h"
#include "../kernel/spinlock.h"
#include "bio.h"

#define FOXFS_MAGIC 0xF00F5555

static foxfs_superblock_t super;
static uint32_t g_dev_id = 0;
static spinlock_t foxfs_lock = SPINLOCK_INIT;

static int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void foxfs_init(void) {
    serial_writeln("[foxfs] engine initialized");
}

// Internal helpers
static int foxfs_read_inode_internal(uint32_t inode_num, foxfs_inode_t* out) {
    uint32_t block = super.inode_table_start + (inode_num * sizeof(foxfs_inode_t)) / BSIZE;
    uint32_t offset = (inode_num * sizeof(foxfs_inode_t)) % BSIZE;
    buf_t* b = bread(g_dev_id, block);
    if (!b) return -1;
    memcopy(out, b->data + offset, sizeof(foxfs_inode_t));
    brelse(b);
    return 0;
}

static int foxfs_write_inode_internal(uint32_t inode_num, foxfs_inode_t* in) {
    uint32_t block = super.inode_table_start + (inode_num * sizeof(foxfs_inode_t)) / BSIZE;
    uint32_t offset = (inode_num * sizeof(foxfs_inode_t)) % BSIZE;
    buf_t* b = bread(g_dev_id, block);
    if (!b) return -1;
    memcopy(b->data + offset, in, sizeof(foxfs_inode_t));
    bwrite(b);
    brelse(b);
    return 0;
}

static uint32_t foxfs_alloc_block(void) {
    for (uint32_t i = 0; i < (super.total_blocks + (BSIZE * 8) - 1) / (BSIZE * 8); i++) {
        buf_t* b = bread(g_dev_id, super.bitmap_start + i);
        uint8_t* bitmap = (uint8_t*)b->data;
        for (uint32_t j = 0; j < BSIZE; j++) {
            if (bitmap[j] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap[j] & (1 << bit))) {
                        bitmap[j] |= (1 << bit);
                        uint32_t block_idx = i * BSIZE * 8 + j * 8 + bit;
                        bwrite(b);
                        brelse(b);
                        super.free_blocks--;
                        return block_idx;
                    }
                }
            }
        }
        brelse(b);
    }
    return 0;
}

int foxfs_format(uint32_t dev_id, uint32_t lba_count) {
    spinlock_acquire(&foxfs_lock);
    g_dev_id = dev_id;
    
    uint32_t total_blocks = (lba_count * 512) / BSIZE;
    uint32_t inode_count = 1024;
    uint32_t inode_blocks = (inode_count * sizeof(foxfs_inode_t) + BSIZE - 1) / BSIZE;
    uint32_t bitmap_blocks = (total_blocks + (BSIZE * 8) - 1) / (BSIZE * 8);
    uint32_t journal_blocks = 128;
    
    super.magic = FOXFS_MAGIC;
    super.version = 1;
    super.block_size = BSIZE;
    super.total_blocks = total_blocks;
    super.inode_count = inode_count;
    super.free_blocks = total_blocks - (1 + inode_blocks + bitmap_blocks + journal_blocks);
    super.free_inodes = inode_count - 1;
    super.inode_table_start = 1;
    super.bitmap_start = 1 + inode_blocks;
    super.journal_start = 1 + inode_blocks + bitmap_blocks;
    super.data_start = 1 + inode_blocks + bitmap_blocks + journal_blocks;
    
    buf_t* b = bread(g_dev_id, 0);
    memcopy(b->data, &super, sizeof(super));
    bwrite(b);
    brelse(b);
    
    for (uint32_t i = 0; i < inode_blocks; i++) {
        b = bread(g_dev_id, super.inode_table_start + i);
        memzero(b->data, BSIZE);
        bwrite(b);
        brelse(b);
    }
    
    for (uint32_t i = 0; i < bitmap_blocks; i++) {
        b = bread(g_dev_id, super.bitmap_start + i);
        memzero(b->data, BSIZE);
        bwrite(b);
        brelse(b);
    }
    
    foxfs_inode_t root_inode;
    memzero(&root_inode, sizeof(root_inode));
    root_inode.inode_num = 0;
    root_inode.type = 2;
    root_inode.mode = 0755;
    root_inode.nlink = 2;
    root_inode.size = 0;
    
    b = bread(g_dev_id, super.inode_table_start);
    memcopy(b->data, &root_inode, sizeof(root_inode));
    bwrite(b);
    brelse(b);
    
    uint32_t used_meta = super.data_start;
    for (uint32_t i = 0; i < used_meta; i++) {
        uint32_t bidx = i / (BSIZE * 8);
        uint32_t bit = i % (BSIZE * 8);
        b = bread(g_dev_id, super.bitmap_start + bidx);
        ((uint8_t*)b->data)[bit / 8] |= (1 << (bit % 8));
        bwrite(b);
        brelse(b);
    }

    spinlock_release(&foxfs_lock);
    serial_writeln("[foxfs] format complete");
    return 0;
}

int foxfs_mount(uint32_t dev_id) {
    g_dev_id = dev_id;
    buf_t* b = bread(g_dev_id, 0);
    if (!b) return -1;
    memcopy(&super, b->data, sizeof(super));
    brelse(b);
    
    if (super.magic != FOXFS_MAGIC) {
        serial_writeln("[foxfs] mount failed: invalid magic");
        return -2;
    }
    serial_writeln("[foxfs] mounted successfully");
    return 0;
}

// VFS Integration Inode bridge
static int foxfs_vfs_read(vfs_inode_t* inode, uint64_t offset, char* buf, uint64_t len, uint64_t* outLen) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    if (offset >= fi->size) { *outLen = 0; return 0; }
    if (offset + len > fi->size) len = fi->size - offset;
    
    uint64_t total_read = 0;
    while (total_read < len) {
        uint32_t lblock = (uint32_t)((offset + total_read) / BSIZE);
        uint32_t loffset = (uint32_t)((offset + total_read) % BSIZE);
        
        uint32_t pblock = 0;
        for (int i = 0; i < 8; i++) {
            if (fi->extents[i].block_count > 0 &&
                lblock >= fi->extents[i].logical_block &&
                lblock < fi->extents[i].logical_block + fi->extents[i].block_count) {
                pblock = fi->extents[i].physical_block + (lblock - fi->extents[i].logical_block);
                break;
            }
        }
        
        if (pblock == 0) {
            uint32_t n = BSIZE - loffset;
            if (n > len - total_read) n = (uint32_t)(len - total_read);
            memzero(buf + total_read, n);
            total_read += n;
            continue;
        }
        
        buf_t* b = bread(g_dev_id, pblock);
        uint32_t n = BSIZE - loffset;
        if (n > len - total_read) n = (uint32_t)(len - total_read);
        memcopy(buf + total_read, b->data + loffset, n);
        brelse(b);
        total_read += n;
    }
    *outLen = total_read;
    return 0;
}

static int foxfs_vfs_write(vfs_inode_t* inode, uint64_t offset, const char* buf, uint64_t len) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    
    uint64_t total_written = 0;
    while (total_written < len) {
        uint32_t lblock = (uint32_t)((offset + total_written) / BSIZE);
        uint32_t loffset = (uint32_t)((offset + total_written) % BSIZE);
        
        uint32_t pblock = 0;
        for (int i = 0; i < 8; i++) {
            if (fi->extents[i].block_count > 0 &&
                lblock >= fi->extents[i].logical_block &&
                lblock < fi->extents[i].logical_block + fi->extents[i].block_count) {
                pblock = fi->extents[i].physical_block + (lblock - fi->extents[i].logical_block);
                break;
            }
        }
        
        if (pblock == 0) {
            pblock = foxfs_alloc_block();
            if (pblock == 0) return -1;
            
            int found = 0;
            for (int i = 0; i < 8; i++) {
                if (fi->extents[i].block_count > 0 && 
                    fi->extents[i].logical_block + fi->extents[i].block_count == lblock &&
                    fi->extents[i].physical_block + fi->extents[i].block_count == pblock) {
                    fi->extents[i].block_count++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                for (int i = 0; i < 8; i++) {
                    if (fi->extents[i].block_count == 0) {
                        fi->extents[i].logical_block = lblock;
                        fi->extents[i].physical_block = pblock;
                        fi->extents[i].block_count = 1;
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) return -2;
        }
        
        buf_t* b = bread(g_dev_id, pblock);
        uint32_t n = BSIZE - loffset;
        if (n > len - total_written) n = (uint32_t)(len - total_written);
        memcopy(b->data + loffset, buf + total_written, n);
        bwrite(b);
        brelse(b);
        total_written += n;
    }
    
    if (offset + len > fi->size) fi->size = offset + len;
    foxfs_write_inode_internal(fi->inode_num, fi);
    return 0;
}

static int foxfs_vfs_stat(vfs_inode_t* inode, vfs_stat_t* st) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    st->exists = 1;
    st->isDir = (fi->type == 2);
    st->size = fi->size;
    st->children = 0;
    return 0;
}

static int foxfs_vfs_ls(vfs_inode_t* inode, vfs_list_cb cb) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    if (fi->type != 2) return -1;
    
    uint8_t* buf = kmalloc(BSIZE);
    uint64_t outLen = 0;
    
    for (uint64_t off = 0; off < fi->size; off += BSIZE) {
        foxfs_vfs_read(inode, off, (char*)buf, BSIZE, &outLen);
        foxfs_dir_entry_t* entries = (foxfs_dir_entry_t*)buf;
        for (uint32_t i = 0; i < BSIZE / sizeof(foxfs_dir_entry_t); i++) {
            if (entries[i].inode_num != 0 && entries[i].inode_num != 0xFFFFFFFF && entries[i].name[0] != 0) {
                foxfs_inode_t ci;
                foxfs_read_inode_internal(entries[i].inode_num, &ci);
                cb(entries[i].name, (ci.type == 2));
            }
        }
    }
    
    kfree(buf);
    return 0;
}

static uint32_t foxfs_alloc_inode(void) {
    for (uint32_t i = 1; i < super.inode_count; i++) {
        foxfs_inode_t in;
        foxfs_read_inode_internal(i, &in);
        if (in.type == 0) {
            super.free_inodes--;
            return i;
        }
    }
    return 0xFFFFFFFF;
}

static int foxfs_vfs_mkdir(vfs_inode_t* inode, const char* name) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    if (fi->type != 2) return -1;
    
    uint32_t inum = foxfs_alloc_inode();
    if (inum == 0xFFFFFFFF) return -2;
    
    foxfs_inode_t new_in;
    memzero(&new_in, sizeof(new_in));
    new_in.inode_num = inum;
    new_in.type = 2; 
    new_in.mode = 0755;
    new_in.nlink = 2;
    new_in.size = 0;
    foxfs_write_inode_internal(inum, &new_in);
    
    foxfs_dir_entry_t entry;
    memzero(&entry, sizeof(entry));
    int k = 0; while (name[k] && k < 59) { entry.name[k] = name[k]; k++; } entry.name[k] = 0;
    entry.inode_num = inum;
    
    foxfs_vfs_write(inode, fi->size, (const char*)&entry, sizeof(entry));
    return 0;
}

static int foxfs_vfs_rm(vfs_inode_t* inode, const char* name) {
    foxfs_inode_t* fi = (foxfs_inode_t*)inode->private_data;
    uint8_t* buf = kmalloc(BSIZE);
    uint64_t outLen = 0;
    
    for (uint64_t off = 0; off < fi->size; off += BSIZE) {
        foxfs_vfs_read(inode, off, (char*)buf, BSIZE, &outLen);
        foxfs_dir_entry_t* entries = (foxfs_dir_entry_t*)buf;
        for (uint32_t i = 0; i < BSIZE / sizeof(foxfs_dir_entry_t); i++) {
            if (entries[i].inode_num != 0xFFFFFFFF && strcmp(entries[i].name, name) == 0) {
                entries[i].inode_num = 0xFFFFFFFF;
                foxfs_vfs_write(inode, off, (const char*)buf, BSIZE);
                kfree(buf);
                return 0;
            }
        }
    }
    kfree(buf);
    return -1;
}

static vfs_ops_t foxfs_ops = {
    .read = foxfs_vfs_read,
    .write = foxfs_vfs_write,
    .stat = foxfs_vfs_stat,
    .ls = foxfs_vfs_ls,
    .mkdir = foxfs_vfs_mkdir,
    .rm = foxfs_vfs_rm
};

static uint32_t foxfs_lookup(uint32_t dir_inode, const char* name) {
    foxfs_inode_t di;
    if (foxfs_read_inode_internal(dir_inode, &di) != 0) return 0xFFFFFFFF;
    if (di.type != 2) return 0xFFFFFFFF;
    
    uint8_t* buf = kmalloc(BSIZE);
    uint64_t outLen = 0;
    
    vfs_inode_t temp_inode;
    temp_inode.private_data = &di;
    temp_inode.ops = &foxfs_ops;
    
    for (uint64_t off = 0; off < di.size; off += BSIZE) {
        foxfs_vfs_read(&temp_inode, off, (char*)buf, BSIZE, &outLen);
        foxfs_dir_entry_t* entries = (foxfs_dir_entry_t*)buf;
        for (uint32_t i = 0; i < BSIZE / sizeof(foxfs_dir_entry_t); i++) {
            if (entries[i].inode_num != 0 && entries[i].inode_num != 0xFFFFFFFF && strcmp(entries[i].name, name) == 0) {
                uint32_t inum = entries[i].inode_num;
                kfree(buf);
                return inum;
            }
        }
    }
    
    kfree(buf);
    return 0xFFFFFFFF;
}

vfs_inode_t* foxfs_get_inode(const char* path) {
    if (path[0] != '/') return NULL;
    if (path[1] == 0) {
        vfs_inode_t* vi = vfs_inode_alloc();
        vi->inode_num = 0;
        vi->ops = &foxfs_ops;
        foxfs_inode_t* fi = kmalloc(sizeof(foxfs_inode_t));
        foxfs_read_inode_internal(0, fi);
        vi->private_data = fi;
        vi->size = fi->size;
        vi->type = fi->type;
        return vi;
    }
    
    uint32_t inum = foxfs_lookup(0, path + 1);
    if (inum != 0xFFFFFFFF) {
        vfs_inode_t* vi = vfs_inode_alloc();
        vi->inode_num = inum;
        vi->ops = &foxfs_ops;
        foxfs_inode_t* fi = kmalloc(sizeof(foxfs_inode_t));
        foxfs_read_inode_internal(inum, fi);
        vi->private_data = fi;
        vi->size = fi->size;
        vi->type = fi->type;
        return vi;
    }
    
    return NULL;
}

int foxfs_defragment(const char* path) {
    vfs_inode_t* vi = foxfs_get_inode(path);
    if (!vi) return -1;
    
    foxfs_inode_t* fi = (foxfs_inode_t*)vi->private_data;
    if (fi->type != 1) { // Only defrag files
        kfree(fi);
        vfs_inode_free(vi);
        return -2;
    }
    
    spinlock_acquire(&foxfs_lock);
    
    serial_write("[foxfs] defragmenting "); serial_writeln(path);
    
    int changed = 0;
    for (int i = 0; i < 7; i++) {
        if (fi->extents[i].block_count == 0) continue;
        for (int j = i + 1; j < 8; j++) {
            if (fi->extents[j].block_count == 0) continue;
            
            // Check if extent j follows extent i logically AND physically
            if (fi->extents[i].logical_block + fi->extents[i].block_count == fi->extents[j].logical_block &&
                fi->extents[i].physical_block + fi->extents[i].block_count == fi->extents[j].physical_block) {
                
                fi->extents[i].block_count += fi->extents[j].block_count;
                fi->extents[j].block_count = 0;
                fi->extents[j].logical_block = 0;
                fi->extents[j].physical_block = 0;
                changed = 1;
            }
        }
    }
    
    if (changed) {
        foxfs_write_inode_internal(fi->inode_num, fi);
        serial_writeln("[foxfs] consolidated extents");
    } else {
        serial_writeln("[foxfs] already optimal");
    }
    
    spinlock_release(&foxfs_lock);
    
    kfree(fi);
    vfs_inode_free(vi);
    return 0;
}

void foxfs_flush_delalloc(void) {}
void foxfs_journal_start(void) {}
void foxfs_journal_commit(void) {}

int foxfs_mkdir(const char* path) {
    if (path[0] != '/') return -1;
    vfs_inode_t* dir = foxfs_get_inode("/");
    if (!dir) return -2;
    int r = dir->ops->mkdir(dir, path + 1);
    // Note: this only works for root-level dirs for now.
    // A real implementation would traverse the path.
    kfree(dir->private_data);
    vfs_inode_free(dir);
    return r;
}

int foxfs_write(const char* path, uint64_t offset, const char* data, uint64_t len) {
    vfs_inode_t* vi = foxfs_get_inode(path);
    if (!vi) {
        // Create file
        vfs_inode_t* root = foxfs_get_inode("/");
        foxfs_inode_t* ri = (foxfs_inode_t*)root->private_data;
        uint32_t inum = foxfs_alloc_inode();
        foxfs_inode_t new_in;
        memzero(&new_in, sizeof(new_in));
        new_in.inode_num = inum;
        new_in.type = 1;
        new_in.mode = 0644;
        new_in.nlink = 1;
        foxfs_write_inode_internal(inum, &new_in);
        
        foxfs_dir_entry_t entry;
        memzero(&entry, sizeof(entry));
        int k = 0; while (path[k+1] && k < 59) { entry.name[k] = path[k+1]; k++; } entry.name[k] = 0;
        entry.inode_num = inum;
        foxfs_vfs_write(root, ri->size, (const char*)&entry, sizeof(entry));
        
        kfree(root->private_data);
        vfs_inode_free(root);
        vi = foxfs_get_inode(path);
    }
    if (!vi) return -1;
    int r = vi->ops->write(vi, offset, data, len);
    kfree(vi->private_data);
    vfs_inode_free(vi);
    return r;
}

int foxfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen) {
    vfs_inode_t* vi = foxfs_get_inode(path);
    if (!vi) return -1;
    int r = vi->ops->read(vi, offset, out, max, outLen);
    kfree(vi->private_data);
    vfs_inode_free(vi);
    return r;
}

int foxfs_rm(const char* path) {
    if (path[0] != '/') return -1;
    vfs_inode_t* root = foxfs_get_inode("/");
    if (!root) return -2;
    int r = root->ops->rm(root, path + 1);
    kfree(root->private_data);
    vfs_inode_free(root);
    return r;
}

int foxfs_ls(const char* path, vfs_list_cb cb) {
    vfs_inode_t* vi = foxfs_get_inode(path);
    if (!vi) return -1;
    int r = vi->ops->ls(vi, cb);
    kfree(vi->private_data);
    vfs_inode_free(vi);
    return r;
}

int foxfs_stat(const char* path, vfs_stat_t* st) {
    vfs_inode_t* vi = foxfs_get_inode(path);
    if (!vi) return -1;
    int r = vi->ops->stat(vi, st);
    kfree(vi->private_data);
    vfs_inode_free(vi);
    return r;
}
