#ifndef ARCH_CC_H
#define ARCH_CC_H
#include "types.h"
// Prevent inclusion of unistd.h by redefining macros
#define LWIP_NO_UNISTD_H 1

// Basic integer types
typedef signed char s8_t;
typedef unsigned char u8_t;
typedef signed short s16_t;
typedef unsigned short u16_t;
typedef signed int s32_t;
typedef unsigned int u32_t;


// Error type
typedef s8_t err_t;

static inline uint64 r_mtime(void) {
    uint64 time;
    asm volatile("csrr %0, time" : "=r" (time));
    return time;
}

// Forward declare printf
int printf(char*, ...) __attribute__ ((format (printf, 1, 2)));
//forward declare panic
void panic(char *) __attribute__((noreturn));

// Diagnostic and assertion macros
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) panic(x)

// Random number generator
#define LWIP_RAND r_mtime

#endif /* ARCH_CC_H */