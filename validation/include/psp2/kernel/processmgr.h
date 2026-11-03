#pragma once
#include <stdint.h>
static inline uint64_t sceKernelGetProcessTimeWide(void){static uint64_t t;return ++t;}
