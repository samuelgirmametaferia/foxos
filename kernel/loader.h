#pragma once
#include <stdint.h>

int sys_exec(const char* path, char* const argv[], char* const envp[]);
