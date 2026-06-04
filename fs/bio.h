#pragma once
#include <stdint.h>
#include "../kernel/spinlock.h"

#define BSIZE 512
#define NBUF 256
#define NBUCKETS 128

#define B_VALID 0x2
#define B_DIRTY 0x4

typedef struct buf {
    uint32_t dev;
    uint32_t blockno;
    int flags;
    uint32_t refcnt;
    struct buf *hash_next;
    struct buf *lru_next;
    struct buf *lru_prev;
    uint8_t data[BSIZE];
    spinlock_t lock;
} buf_t;

typedef struct request {
    uint32_t dev;
    uint32_t blockno;
    uint32_t count;      // blocks
    int is_write;
    void* buf;           // DMA physical addr (if needed) or logical
    struct request* next;
    int done;
} request_t;

#define MAX_BLK_DEVICES 16

struct blk_device;

typedef struct blk_device {
    int (*read)(struct blk_device* self, uint64_t lba, void* buf, uint32_t count);
    int (*write)(struct blk_device* self, uint64_t lba, const void* buf, uint32_t count);
    void* private_data;
    spinlock_t lock;
    struct blk_device* parent;
} blk_device_t;

void binit(void);
void blk_register_device(uint32_t dev_id, blk_device_t* dev);
blk_device_t* blk_get_device(uint32_t dev_id);
buf_t* bread(uint32_t dev, uint32_t blockno);
void bwrite(buf_t* b);
void brelse(buf_t* b);
void bflush(void);
void kflushtd_init(void);

// Block layer MQ functions
void blk_mq_submit(uint32_t dev, uint32_t blockno, uint32_t count, void* buf, int is_write);
