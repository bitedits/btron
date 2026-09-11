#include <stdint.h>
/*
 * T-Kernel 2.0 Standard String & Memory Library: libstr.c
 */

#include <basic.h>
#include <libstr.h>
#include <stdarg.h>

__attribute__((weak)) void* tkl_memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

__attribute__((weak)) void* tkl_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((weak)) void* tkl_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int tkl_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

size_t tkl_strlen(const char *s) {
    size_t len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

char* tkl_strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

char* tkl_strncpy(char *dst, const char *src, size_t n) {
    char *ret = dst;
    volatile char *d = (volatile char *)dst;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return ret;
}

int tkl_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int tkl_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* tkl_strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char* tkl_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

/* Heap Memory Allocator (Icalloc / Ifree / Imalloc) */
#define HEAP_MAGIC 0x48454150

typedef struct HeapBlock {
    size_t size;
    uint32_t is_free;
    uint32_t magic;
    struct HeapBlock *next;
    struct HeapBlock *prev;
    uint8_t pad[16];
} __attribute__((aligned(16))) HeapBlock;

#if defined(__mips__)
static uint8_t s_heap_pool[1024 * 1024] __attribute__((aligned(16)));
#elif !defined(_RPI_BCM283x_) && !defined(__arm__) && !defined(__aarch64__)
static uint8_t s_heap_pool[1024 * 1024 * 16] __attribute__((aligned(16)));
#else
static uint8_t s_heap_pool[64] __attribute__((aligned(16)));
#endif
static size_t  s_heap_offset = 0;
static HeapBlock *s_heap_head = NULL;

__attribute__((weak)) void* Imalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 15) & ~15; /* 16-byte alignment */

    /* 1. First-fit search in existing block list */
    HeapBlock *curr = s_heap_head;
    while (curr) {
        if (curr->magic == HEAP_MAGIC && curr->is_free && curr->size >= size) {
            /* If remaining capacity is large enough, split */
            if (curr->size >= size + sizeof(HeapBlock) + 16) {
                HeapBlock *split = (HeapBlock*)((uint8_t*)(curr + 1) + size);
                split->size = curr->size - size - sizeof(HeapBlock);
                split->is_free = 1;
                split->magic = HEAP_MAGIC;
                split->next = curr->next;
                split->prev = curr;
                if (curr->next) curr->next->prev = split;
                curr->next = split;
                curr->size = size;
            }
            curr->is_free = 0;
            return (void*)(curr + 1);
        }
        curr = curr->next;
    }

    /* 2. Allocate from bump pool */
    size_t needed = sizeof(HeapBlock) + size;
    needed = (needed + 15) & ~15;
    if (s_heap_offset + needed > sizeof(s_heap_pool)) {
        return NULL;
    }

    HeapBlock *blk = (HeapBlock*)&s_heap_pool[s_heap_offset];
    s_heap_offset += needed;

    blk->size = size;
    blk->is_free = 0;
    blk->magic = HEAP_MAGIC;
    blk->next = NULL;
    blk->prev = NULL;

    if (!s_heap_head) {
        s_heap_head = blk;
    } else {
        HeapBlock *tail = s_heap_head;
        while (tail->next) tail = tail->next;
        tail->next = blk;
        blk->prev = tail;
    }

    return (void*)(blk + 1);
}

__attribute__((weak)) void* Icalloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = Imalloc(total);
    if (p) tkl_memset(p, 0, total);
    return p;
}

__attribute__((weak)) void Ifree(void *ptr) {
    if (!ptr) return;
    HeapBlock *blk = (HeapBlock*)ptr - 1;
    if (blk->magic != HEAP_MAGIC) return;

    blk->is_free = 1;

    /* Coalesce forward */
    if (blk->next && blk->next->is_free && blk->next->magic == HEAP_MAGIC) {
        HeapBlock *nxt = blk->next;
        blk->size += sizeof(HeapBlock) + nxt->size;
        blk->next = nxt->next;
        if (nxt->next) nxt->next->prev = blk;
    }

    /* Coalesce backward */
    if (blk->prev && blk->prev->is_free && blk->prev->magic == HEAP_MAGIC) {
        HeapBlock *prv = blk->prev;
        prv->size += sizeof(HeapBlock) + blk->size;
        prv->next = blk->next;
        if (blk->next) blk->next->prev = prv;
        blk = prv;
    }

    /* Shrink bump pointer if at the end */
    if (blk->next == NULL && blk->is_free) {
        if (blk->prev) {
            blk->prev->next = NULL;
        } else {
            s_heap_head = NULL;
        }
        s_heap_offset = (size_t)((uint8_t*)blk - s_heap_pool);
    }
}

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
/* Standard aliases only for freestanding mode */
void* memset(void *s, int c, size_t n) __attribute__((weak, alias("tkl_memset")));
void* memcpy(void *dst, const void *src, size_t n) __attribute__((weak, alias("tkl_memcpy")));
void* memmove(void *dst, const void *src, size_t n) __attribute__((weak, alias("tkl_memmove")));
int   memcmp(const void *s1, const void *s2, size_t n) __attribute__((weak, alias("tkl_memcmp")));
size_t strlen(const char *s) __attribute__((weak, alias("tkl_strlen")));
char*  strcpy(char *dst, const char *src) __attribute__((weak, alias("tkl_strcpy")));
char*  strncpy(char *dst, const char *src, size_t n) __attribute__((weak, alias("tkl_strncpy")));
int    strcmp(const char *s1, const char *s2) __attribute__((weak, alias("tkl_strcmp")));
int    strncmp(const char *s1, const char *s2, size_t n) __attribute__((weak, alias("tkl_strncmp")));
#endif

char* tkl_strcat(char *dst, const char *src) {
    char *ret = dst;
    while (*dst) dst++;
    while ((*dst++ = *src++));
    return ret;
}

char* tkl_strncat(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (*dst) dst++;
    while (n-- && *src) *dst++ = *src++;
    *dst = '\0';
    return ret;
}

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
char* strcat(char *dst, const char *src) __attribute__((weak, alias("tkl_strcat")));
char* strncat(char *dst, const char *src, size_t n) __attribute__((weak, alias("tkl_strncat")));
char* strchr(const char *s, int c) __attribute__((weak, alias("tkl_strchr")));
char* strstr(const char *haystack, const char *needle) __attribute__((weak, alias("tkl_strstr")));
#endif

/* Minimal standalone vsnprintf / snprintf */
__attribute__((weak)) int tkl_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    char *out = str;
    char *end = str + size - 1;

    while (*format && out < end) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        format++;
        if (*format == '\0') break;

        /* Skip width / formatting flags like %-18s, %02d, %d, %s, %x, %p */
        int width = 0;
        int left_align = 0;
        if (*format == '-') { left_align = 1; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        if (*format == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = (int)tkl_strlen(s);
            while (*s && out < end) *out++ = *s++;
            if (left_align && width > slen) {
                for (int pad = 0; pad < width - slen && out < end; pad++) *out++ = ' ';
            }
        } else if (*format == 'd' || *format == 'i' || *format == 'u') {
            int val = va_arg(ap, int);
            char tmp[32];
            int p = 0;
            if (val == 0) tmp[p++] = '0';
            else {
                unsigned int uval = (unsigned int)val;
                if (*format != 'u' && val < 0) {
                    if (out < end) *out++ = '-';
                    uval = (unsigned int)(-val);
                }
                while (uval > 0) {
                    tmp[p++] = (char)('0' + (uval % 10));
                    uval /= 10;
                }
            }
            while (p > 0 && out < end) *out++ = tmp[--p];
        } else if (*format == 'x' || *format == 'X' || *format == 'p') {
            unsigned int val = va_arg(ap, unsigned int);
            char tmp[32];
            int p = 0;
            if (val == 0) tmp[p++] = '0';
            else {
                while (val > 0) {
                    int digit = val & 0xF;
                    tmp[p++] = (char)((digit < 10) ? ('0' + digit) : ('a' + digit - 10));
                    val >>= 4;
                }
            }
            while (p > 0 && out < end) *out++ = tmp[--p];
        } else if (*format == 'c') {
            char c = (char)va_arg(ap, int);
            *out++ = c;
        } else if (*format == '%') {
            *out++ = '%';
        }
        format++;
    }

    *out = '\0';
    return (int)(out - str);
}

__attribute__((weak)) int tkl_snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = tkl_vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
int snprintf(char *str, size_t size, const char *format, ...) __attribute__((weak, alias("tkl_snprintf")));
#endif

/* Standard strtoul implementation */
__attribute__((weak)) unsigned long int tkl_strtoul(const char *nptr, char **endptr, int base) {
    unsigned long int value = 0;
    int sign = 1;
    int i;

    if (!nptr) {
        if (endptr) *endptr = NULL;
        return 0;
    }

    while (*nptr == ' ' || *nptr == '\t') {
        ++nptr;
    }

    switch (*nptr) {
    case '-':
        sign = -1;
        /* fallthrough */
    case '+':
        ++nptr;
        break;
    default:
        break;
    }

    if (base == 16) {
        if (*nptr == '0' && (*(nptr + 1) == 'X' || *(nptr + 1) == 'x')) {
            nptr += 2;
        }
    } else if (base == 0) {
        if (*nptr == '0') {
            ++nptr;
            if (*nptr == 'X' || *nptr == 'x') {
                ++nptr;
                base = 16;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base < 2 || base > 36) {
        base = 10;
    }

    while (*nptr != '\0') {
        if (*nptr >= '0' && *nptr <= '9') {
            i = *nptr - '0';
        } else if (*nptr >= 'A' && *nptr <= 'Z') {
            i = *nptr - 'A' + 10;
        } else if (*nptr >= 'a' && *nptr <= 'z') {
            i = *nptr - 'a' + 10;
        } else {
            break;
        }
        if (i >= base) {
            break;
        }
        value = value * base + i;
        ++nptr;
    }
    if (endptr != NULL) {
        *endptr = (char *)nptr;
    }
    return value * sign;
}

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
unsigned long int strtoul(const char *nptr, char **endptr, int base) __attribute__((weak, alias("tkl_strtoul")));
#endif
