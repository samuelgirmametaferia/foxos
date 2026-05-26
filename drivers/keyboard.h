#pragma once
#include <stdint.h>

void keyboard_init(void);
int keyboard_getchar(void); // returns -1 if no key
int keyboard_getchar_blocking(void);  // blocks until key available

// Special keys (negative values)
#define KBD_KEY_UP   (-1001)
#define KBD_KEY_DOWN (-1002)
