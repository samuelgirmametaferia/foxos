#include "relocation.h"
#include "memory.h"

/* Simple relocation/compaction helper: shrink heaps then try to defragment UC allocations.
   Returns number of UC handles moved. */
int relocation_compact(void) {
    heap_shrink_all();
    return move_defrag_all();
}
