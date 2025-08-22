#pragma once
#include <stdint.h>

void keyboard_init(void);
int keyboard_getchar(void); // returns -1 if no key
