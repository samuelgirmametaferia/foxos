#include "quiesce.h"
#include "serial.h"

#define MAX_QUIESCE 32

static quiesce_cb_t qcb[MAX_QUIESCE];

int quiesce_register(quiesce_cb_t cb) {
    if (!cb) return -1;
    for (int i = 0; i < MAX_QUIESCE; ++i) {
        if (qcb[i] == cb) return 0; /* already registered */
        if (qcb[i] == NULL) { qcb[i] = cb; serial_write("[quiesce] registered\n"); return 0; }
    }
    return -1; /* full */
}

int quiesce_unregister(quiesce_cb_t cb) {
    if (!cb) return -1;
    for (int i = 0; i < MAX_QUIESCE; ++i) {
        if (qcb[i] == cb) { qcb[i] = NULL; serial_write("[quiesce] unregistered\n"); return 0; }
    }
    return -1;
}

void quiesce_enter_all(void) {
    serial_writeln("[quiesce] enter_all");
    for (int i = 0; i < MAX_QUIESCE; ++i) {
        if (qcb[i]) {
            qcb[i](1);
        }
    }
}

void quiesce_exit_all(void) {
    serial_writeln("[quiesce] exit_all");
    for (int i = 0; i < MAX_QUIESCE; ++i) {
        if (qcb[i]) {
            qcb[i](0);
        }
    }
}
