#pragma once
#include "../common/boot.h"
#include <stdint.h>

void gpu_init(const boot_info_t* boot);
uint32_t gpu_get_width(void);
uint32_t gpu_get_height(void);
