
#include "app/app.h"
#include "translator.h"
#include "virtual_space/vpage.h"

#include <stddef.h>

extern void *__real_memcpy(void *dst, const void *src, size_t n);
extern void *__real_memmove(void *dst, const void *src, size_t n);
extern void *__real_memset(void *dst, int c, size_t n);
extern int __real_memcmp(const void *a, const void *b, size_t n);
extern size_t __real_strlen(const char *s);
extern char *__real_strcpy(char *dst, const char *src);
extern char *__real_strncpy(char *dst, const char *src, size_t n);
extern int __real_strcmp(const char *a, const char *b);
extern int __real_strncmp(const char *a, const char *b, size_t n);

void *__wrap_memcpy(void *dst, const void *src, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check(dst, n, PERM_WRITE);
        vmmu_check((void *)src, n, PERM_READ);
    }
    return __real_memcpy(dst, src, n);
}

void *__wrap_memmove(void *dst, const void *src, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check(dst, n, PERM_WRITE);
        vmmu_check((void *)src, n, PERM_READ);
    }
    return __real_memmove(dst, src, n);
}

void *__wrap_memset(void *dst, int c, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check(dst, n, PERM_WRITE);
    }
    return __real_memset(dst, c, n);
}

int __wrap_memcmp(const void *a, const void *b, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check((void *)a, n, PERM_READ);
        vmmu_check((void *)b, n, PERM_READ);
    }
    return __real_memcmp(a, b, n);
}

size_t __wrap_strlen(const char *s)
{
    if(!app_current())
    {
        return __real_strlen(s);
    }
    size_t len = 0;
    while(1)
    {
        vmmu_check((void *)(s + len), 1, PERM_READ);
        if(s[len] == '\0')
        {
            return len;
        }
        len++;
    }
}

char *__wrap_strcpy(char *dst, const char *src)
{
    if(!app_current())
    {
        return __real_strcpy(dst, src);
    }
    size_t i = 0;
    while(1)
    {
        vmmu_check((void *)(src + i), 1, PERM_READ);
        vmmu_check((void *)(dst + i), 1, PERM_WRITE);
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            break;
        }
        i++;
    }
    return dst;
}

char *__wrap_strncpy(char *dst, const char *src, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check((void *)src, n, PERM_READ);
        vmmu_check(dst, n, PERM_WRITE);
    }
    return __real_strncpy(dst, src, n);
}

int __wrap_strcmp(const char *a, const char *b)
{
    if(!app_current())
    {
        return __real_strcmp(a, b);
    }
    size_t i = 0;
    while(1)
    {
        vmmu_check((void *)(a + i), 1, PERM_READ);
        vmmu_check((void *)(b + i), 1, PERM_READ);
        if(a[i] != b[i])
        {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        if(a[i] == '\0')
        {
            return 0;
        }
        i++;
    }
}

int __wrap_strncmp(const char *a, const char *b, size_t n)
{
    if(app_current() && n > 0)
    {
        vmmu_check((void *)a, n, PERM_READ);
        vmmu_check((void *)b, n, PERM_READ);
    }
    return __real_strncmp(a, b, n);
}
