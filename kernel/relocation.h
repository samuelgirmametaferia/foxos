#ifndef FOXOS_RELOCATION_H
#define FOXOS_RELOCATION_H

#include <stdint.h>

typedef struct {
    int last_moved;
    int last_passes;
    int running;
} relocation_status_t;

/* Run relocation/compaction up to max_passes. Returns total moved handles. */
int relocation_compact_multi(int max_passes);

/* Backwards-compatible single-call API */
int relocation_compact(void);

/* Query last relocation status */
relocation_status_t relocation_get_status(void);

#endif
