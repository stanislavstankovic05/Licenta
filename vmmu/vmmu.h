#ifndef VMMU_H
#define VMMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHADOW_UNOWNED 0xFFu

void vmmu_init(void);

void shadow_mem_mark(uintptr_t paddr, size_t len, uint8_t app_id);
void shadow_mem_clear(uintptr_t paddr, size_t len);
int shadow_mem_check(uintptr_t paddr, size_t len, uint8_t app_id);
bool vmmu_in_pool(uintptr_t paddr);
void shadow_mem_reclaim_owner(uint8_t app_id);

#endif
