#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <stddef.h>
#include <stdint.h>

extern uint32_t __vmmu_peak_frames;
extern uint32_t __vmmu_globals_size;

void vmmu_check(void *ptr, size_t len, uint8_t access);
void vmmu_stack_check(void);
void *vmmu_globals_base(void);
void *vmmu_malloc(size_t size);
void vmmu_free(void *ptr);

#endif
