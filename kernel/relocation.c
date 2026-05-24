#include "relocation.h"
#include "memory.h"
#include "serial.h"
#include "quiesce.h"

static relocation_status_t g_status = {0,0,0};

/* Run relocation/compaction up to max_passes. Returns total moved handles. */
int relocation_compact_multi(int max_passes) {
    if (max_passes <= 0) max_passes = 1;
    g_status.running = 1;
    g_status.last_moved = 0;
    g_status.last_passes = 0;

    /* ask subsystems to quiesce before compaction */
    quiesce_enter_all();

    for (int pass = 0; pass < max_passes; ++pass) {
        heap_shrink_all();
        int moved = move_defrag_all();
        g_status.last_moved += moved;
        g_status.last_passes = pass + 1;
        serial_write("[reloc] pass "); serial_writeln("done");
        if (moved == 0) break;
    }

    /* resume subsystems */
    quiesce_exit_all();

    g_status.running = 0;
    return g_status.last_moved;
}

int relocation_compact(void) {
    return relocation_compact_multi(3);
}

relocation_status_t relocation_get_status(void) {
    return g_status;
}
