#include <stddef.h>
#include <stdint.h>
#include "memory.h"

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)c;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    if (d == s) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i-- > 0;) d[i] = s[i];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t*)a;
    const uint8_t *pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

void abort(void) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void __stack_chk_fail(void) {
    abort();
}

long sysconf(long name) {
    (void)name;
    return 4096;
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

void free(void *ptr) {
    kfree(ptr);
}

void *realloc(void *ptr, size_t size) {
    return krealloc(ptr, (uint32_t)size);
}

typedef int pthread_once_t;
typedef int pthread_key_t;
typedef int pthread_mutex_t;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
    (void)destructor;
    if (key) *key = 0;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    (void)key;
    (void)value;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    (void)key;
    return 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (once_control && *once_control == 0) {
        *once_control = 1;
        if (init_routine) init_routine();
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}
