#include "bio.h"
#include "../drivers/ata.h"
#include "../kernel/memory.h"
#include "../drivers/serial.h"
#include "../kernel/sched.h"
#include "../kernel/smp.h"

static buf_t bufs[NBUF];
static buf_t* hash_table[NBUCKETS];
static spinlock_t hash_locks[NBUCKETS];

static buf_t lru_head;
static spinlock_t lru_lock = SPINLOCK_INIT;

// Multi-queue structures
#define MAX_CORES 16

typedef struct {
    spinlock_t lock;
    request_t* head;
} blk_queue_t;

static blk_queue_t ssq[MAX_CORES];
static blk_device_t* g_blk_devices[MAX_BLK_DEVICES];

static inline uint32_t hash(uint32_t dev, uint32_t blockno) {
    return (dev ^ blockno) % NBUCKETS;
}

void binit(void) {
    for(int i = 0; i < NBUCKETS; i++) {
        hash_locks[i].locked = 0;
        hash_table[i] = 0;
    }
    
    lru_head.lru_prev = &lru_head;
    lru_head.lru_next = &lru_head;
    
    for(int i = 0; i < NBUF; i++) {
        bufs[i].lock.locked = 0;
        bufs[i].flags = 0;
        bufs[i].refcnt = 0;
        bufs[i].lru_next = lru_head.lru_next;
        bufs[i].lru_prev = &lru_head;
        lru_head.lru_next->lru_prev = &bufs[i];
        lru_head.lru_next = &bufs[i];
    }

    for (int i = 0; i < MAX_CORES; i++) {
        ssq[i].lock.locked = 0;
        ssq[i].head = 0;
    }
    for (int i = 0; i < MAX_BLK_DEVICES; i++) {
        g_blk_devices[i] = 0;
    }
}

void blk_register_device(uint32_t dev_id, blk_device_t* dev) {
    if (dev_id < MAX_BLK_DEVICES) {
        if (dev) {
            dev->lock.locked = 0;
            // Only set parent to NULL if it hasn't been set by the caller
            // This is hacky but since we don't have a proper struct init...
            // Actually, most callers will have it as NULL anyway if it's static.
        }
        g_blk_devices[dev_id] = dev;
    }
}

blk_device_t* blk_get_device(uint32_t dev_id) {
    if (dev_id < MAX_BLK_DEVICES) return g_blk_devices[dev_id];
    return 0;
}

// C-LOOK Elevator merging
void blk_mq_submit(uint32_t dev, uint32_t blockno, uint32_t count, void* buf, int is_write) {
    if (dev >= MAX_BLK_DEVICES || !g_blk_devices[dev]) return;
    
    serial_write("[bio] submit dev="); serial_u64(dev); serial_write(" block="); serial_u64(blockno); serial_writeln(is_write?" W":" R");

    extern int smp_get_core_id(void);
    uint32_t core = (uint32_t)smp_get_core_id();
    if (core >= MAX_CORES) core = 0;
    
    request_t* req = (request_t*)kmalloc(sizeof(request_t));
    req->dev = dev;
    req->blockno = blockno;
    req->count = count;
    req->is_write = is_write;
    req->buf = buf;
    req->done = 0;
    req->next = 0;

    spinlock_acquire(&ssq[core].lock);
    
    // Sort into C-LOOK queue
    request_t** p = &ssq[core].head;
    while (*p && (*p)->blockno < req->blockno) {
        // Attempt merge contiguous
        if ((*p)->is_write == req->is_write && (*p)->blockno + (*p)->count == req->blockno) {
            (*p)->count += req->count;
            kfree(req);
            spinlock_release(&ssq[core].lock);
            // Wait for completion
            while(!(*p)->done) { scheduler_thread_sleep(scheduler_current_thread_id(), 1); }
            return;
        }
        p = &(*p)->next;
    }
    
    req->next = *p;
    *p = req;
    
    spinlock_release(&ssq[core].lock);

    // Process the queue until our request is done.
    while (!req->done) {
        spinlock_acquire(&ssq[core].lock);
        request_t* to_run = ssq[core].head;
        if (to_run) {
            ssq[core].head = to_run->next;
            spinlock_release(&ssq[core].lock);

            // Execute via registered driver (ensure atomic access to hardware via root lock)
            blk_device_t* bdev = g_blk_devices[to_run->dev];
            blk_device_t* root = bdev;
            while (root && root->parent) root = root->parent;

            if (root) spinlock_acquire(&root->lock);
            if (to_run->is_write) {
                bdev->write(bdev, to_run->blockno * (BSIZE/512), to_run->buf, to_run->count * (BSIZE/512));
            } else {
                bdev->read(bdev, to_run->blockno * (BSIZE/512), to_run->buf, to_run->count * (BSIZE/512));
            }
            if (root) spinlock_release(&root->lock);
            to_run->done = 1;
            
            // If we processed OUR request, we can free it and return.
            // Note: if another thread processed it, they will NOT free it, 
            // the owner (the thread that entered blk_mq_submit) must free it.
            // But wait, the thread that OWNS req is currently in this loop!
        } else {
            spinlock_release(&ssq[core].lock);
            scheduler_thread_sleep(scheduler_current_thread_id(), 1);
        }
    }
    
    // Request is done. If it was NOT merged (if it was merged, it was already freed), free it now.
    kfree(req);
}

static buf_t* bget(uint32_t dev, uint32_t blockno) {
    uint32_t h = hash(dev, blockno);
    spinlock_acquire(&hash_locks[h]);

    for (buf_t* b = hash_table[h]; b; b = b->hash_next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            spinlock_release(&hash_locks[h]);
            spinlock_acquire(&b->lock);
            return b;
        }
    }

    spinlock_acquire(&lru_lock);
    for (buf_t* b = lru_head.lru_prev; b != &lru_head; b = b->lru_prev) {
        if (b->refcnt == 0 && !(b->flags & B_DIRTY)) {
            // Found a victim
            uint32_t old_h = hash(b->dev, b->blockno);
            if (old_h != h) {
                if (!spinlock_try_acquire(&hash_locks[old_h])) {
                    continue; // Avoid deadlock, try next
                }
            }

            // Remove from old hash
            buf_t** pp = &hash_table[old_h];
            while (*pp && *pp != b) pp = &(*pp)->hash_next;
            if (*pp) *pp = b->hash_next;

            if (old_h != h) spinlock_release(&hash_locks[old_h]);

            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            
            b->hash_next = hash_table[h];
            hash_table[h] = b;

            spinlock_release(&lru_lock);
            spinlock_release(&hash_locks[h]);
            spinlock_acquire(&b->lock);
            return b;
        }
    }
    
    serial_writeln("PANIC: bget: no buffers");
    while(1);
    return 0;
}

buf_t* bread(uint32_t dev, uint32_t blockno) {
    buf_t* b = bget(dev, blockno);
    if (!(b->flags & B_VALID)) {
        blk_mq_submit(dev, blockno, 1, b->data, 0);
        b->flags |= B_VALID;
    }
    return b;
}

void bwrite(buf_t* b) {
    b->flags |= B_DIRTY;
}

void brelse(buf_t* b) {
    spinlock_acquire(&lru_lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        // Move to MRU position (head of LRU list)
        b->lru_next->lru_prev = b->lru_prev;
        b->lru_prev->lru_next = b->lru_next;
        b->lru_next = lru_head.lru_next;
        b->lru_prev = &lru_head;
        lru_head.lru_next->lru_prev = b;
        lru_head.lru_next = b;
    }
    spinlock_release(&lru_lock);
    spinlock_release(&b->lock);
}

void bflush(void) {
    for (int i = 0; i < NBUF; i++) {
        buf_t* b = &bufs[i];
        if (!spinlock_try_acquire(&b->lock)) continue;
        if ((b->flags & B_VALID) && (b->flags & B_DIRTY)) {
            blk_mq_submit(b->dev, b->blockno, 1, b->data, 1);
            b->flags &= ~B_DIRTY;
        }
        spinlock_release(&b->lock);
    }
}

static void kflushtd(void) {
    serial_writeln("[bio] Background flusher thread started");
    while (1) {
        bflush();
        scheduler_thread_sleep(scheduler_current_thread_id(), 5000);
    }
}

void kflushtd_init(void) {
    scheduler_create(kflushtd);
}
