#ifndef SHM_COMMON_H
#define SHM_COMMON_H

#include <stdint.h>

#define SHM_SIZE 4096

typedef struct {
    uint32_t length;
    char data[SHM_SIZE - sizeof(uint32_t)];
} shm_t;

#endif
