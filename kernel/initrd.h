#pragma once
#include <stdint.h>

extern const unsigned char initrd_blob[];
extern const unsigned int initrd_size;

void initrd_load_into_ramfs(void);
