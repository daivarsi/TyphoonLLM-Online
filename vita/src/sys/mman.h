/*
 * Minimal mmap compatibility shim for Vita builds.
 *
 * llama2.c/run.c declares an mmap-based loader for desktop Unix. WoozyLLM Vita
 * does not use that path; it loads model.bin with fopen + malloc instead.
 */

#ifndef WOOZYLLM_SYS_MMAN_H
#define WOOZYLLM_SYS_MMAN_H

#include <stddef.h>

#define PROT_READ 1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    (void)addr;
    (void)len;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)off;
    return MAP_FAILED;
}

static inline int munmap(void *addr, size_t len)
{
    (void)addr;
    (void)len;
    return -1;
}

#endif /* WOOZYLLM_SYS_MMAN_H */
