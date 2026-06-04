#pragma once
#include <stdint.h>

void run_alloc_stress(void);
void run_boot_self_tests(void);
void run_stability_test(void);
void run_scheduler_tests(void);
void run_interrupt_stability_tests(void);
void run_multicore_detection_test(void);
void run_io_integration_test(void);

void run_smptest(void);
void run_dmatest(void);
void run_cachetest(void);
void run_foxfs_bench(void);
void run_biotest(void);
void run_vfstest(void);
void run_foxfstest(void);
void run_concurrencytest(void);
void run_crashrecoverytest(void);
void run_defragdmatest(void);
