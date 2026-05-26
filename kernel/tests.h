#pragma once
#include <stdint.h>

void run_alloc_stress(void);
void run_boot_self_tests(void);
void run_scheduler_tests(void);
void run_interrupt_stability_tests(void);
void run_multicore_detection_test(void);
void run_io_integration_test(void);
