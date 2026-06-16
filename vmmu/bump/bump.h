#ifndef BUMP_H
#define BUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void bump_init(uintptr_t base, uintptr_t limit);
void *bump_alloc(size_t size, size_t align);
uintptr_t bump_get_current(void);
uintptr_t bump_get_limit(void);

#endif
