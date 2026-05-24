#ifndef FOXOS_QUIESCE_H
#define FOXOS_QUIESCE_H

#include <stdint.h>

typedef void (*quiesce_cb_t)(int enter);

int quiesce_register(quiesce_cb_t cb);
int quiesce_unregister(quiesce_cb_t cb);

/* Request all registered subsystems to quiesce (enter=1) */
void quiesce_enter_all(void);
/* Request all registered subsystems to resume (enter=0) */
void quiesce_exit_all(void);

#endif
