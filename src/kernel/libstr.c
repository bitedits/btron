#include <stdint.h>
/*
 * T-Kernel 2.0 Standard String & Memory Library: libstr.c
 */

#include <basic.h>
#include <libstr.h>
#include <stdarg.h>

__attribute__((weak)) void* tkl_memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char uc = (unsigned char)c;

    if (n >= 16) {
        while (((uintptr_t)p & 7) && n) {
            *p++ = uc;
            n--;
        }
        uint64_t val64 = uc;
        val64 |= (val64 << 8);
        val64 |= (val64 << 16);
        val64 |= (val64 << 32);

        uint64_t *p64 = (uint64_t *)p;
        while (n >= 64) {
            p64[0] = val64;
            p64[1] = val64;
            p64[2] = val64;
            p64[3] = val64;
            p64[4] = val64;
            p64[5] = val64;
            p64[6] = val64;
            p64[7] = val64;
            p64 += 8;
            n -= 64;
        }
        while (n >= 8) {
            *p64++ = val64;
            n -= 8;
        }
        p = (unsigned char *)p64;
    }
    while (n--) *p++ = uc;
    return s;
}

__attribute__((weak)) void* tkl_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (n >= 16 && ((((uintptr_t)d ^ (uintptr_t)s) & 7) == 0)) {
        while (((uintptr_t)d & 7) && n) {
            *d++ = *s++;
            n--;
        }
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        while (n >= 64) {
            d64[0] = s64[0];
            d64[1] = s64[1];
            d64[2] = s64[2];
            d64[3] = s64[3];
            d64[4] = s64[4];
            d64[5] = s64[5];
            d64[6] = s64[6];
            d64[7] = s64[7];
            d64 += 8;
            s64 += 8;
            n -= 64;
        }
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        d = (unsigned char *)d64;
        s = (const unsigned char *)s64;
    } else if (n >= 8 && ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0)) {
        while (((uintptr_t)d & 3) && n) {
            *d++ = *s++;
            n--;
        }
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;
        while (n >= 16) {
            d32[0] = s32[0];
            d32[1] = s32[1];
            d32[2] = s32[2];
            d32[3] = s32[3];
            d32 += 4;
            s32 += 4;
            n -= 16;
        }
        while (n >= 4) {
            *d32++ = *s32++;
            n -= 4;
        }
        d = (unsigned char *)d32;
        s = (const unsigned char *)s32;
    }
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((weak)) void* tkl_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;

    if (d < s) {
        if (n >= 16 && ((((uintptr_t)d ^ (uintptr_t)s) & 7) == 0)) {
            while (((uintptr_t)d & 7) && n) {
                *d++ = *s++;
                n--;
            }
            uint64_t *d64 = (uint64_t *)d;
            const uint64_t *s64 = (const uint64_t *)s;
            while (n >= 64) {
                d64[0] = s64[0];
                d64[1] = s64[1];
                d64[2] = s64[2];
                d64[3] = s64[3];
                d64[4] = s64[4];
                d64[5] = s64[5];
                d64[6] = s64[6];
                d64[7] = s64[7];
                d64 += 8;
                s64 += 8;
                n -= 64;
            }
            while (n >= 8) {
                *d64++ = *s64++;
                n -= 8;
            }
            d = (unsigned char *)d64;
            s = (const unsigned char *)s64;
        }
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        if (n >= 16 && ((((uintptr_t)d ^ (uintptr_t)s) & 7) == 0)) {
            while (((uintptr_t)d & 7) && n) {
                *--d = *--s;
                n--;
            }
            uint64_t *d64 = (uint64_t *)d;
            const uint64_t *s64 = (const uint64_t *)s;
            while (n >= 64) {
                d64 -= 8;
                s64 -= 8;
                d64[7] = s64[7];
                d64[6] = s64[6];
                d64[5] = s64[5];
                d64[4] = s64[4];
                d64[3] = s64[3];
                d64[2] = s64[2];
                d64[1] = s64[1];
                d64[0] = s64[0];
                n -= 64;
            }
            while (n >= 8) {
                *--d64 = *--s64;
                n -= 8;
            }
            d = (unsigned char *)d64;
            s = (const unsigned char *)s64;
        }
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
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char* tkl_strrchr(const char *s, int c) {
    const char *last = NULL;
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)last;
}

char* tkl_strpbrk(const char *s, const char *accept) {
    if (!s || !accept) return NULL;
    for (; *s != '\0'; s++) {
        for (const char *a = accept; *a != '\0'; a++) {
            if (*s == *a) return (char *)s;
        }
    }
    return NULL;
}

int tkl_atoi(const char *nptr) {
    return (int)tkl_strtoul(nptr, NULL, 10);
}

char* tkl_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
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
char* strrchr(const char *s, int c) __attribute__((weak, alias("tkl_strrchr")));
char* strpbrk(const char *s, const char *accept) __attribute__((weak, alias("tkl_strpbrk")));
char* strstr(const char *haystack, const char *needle) __attribute__((weak, alias("tkl_strstr")));
int     atoi(const char *nptr) __attribute__((weak, alias("tkl_atoi")));
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

/* ── qsort ──────────────────────────────────────────────────────── */

/* Swapped a byte at a time: the element width is the caller's business, and this
 * needs no scratch buffer, so nothing here can fail on a large `width`. */
static void tkl_qswap(unsigned char *x, unsigned char *y, size_t width)
{
    size_t i;
    for (i = 0; i < width; i++) {
        unsigned char t = x[i];
        x[i] = y[i];
        y[i] = t;
    }
}

static void tkl_qinsert(unsigned char *a, size_t lo, size_t hi, size_t width,
                        int (*compar)(const void *, const void *))
{
    size_t i, j;
    for (i = lo + 1u; i < hi; i++)
        for (j = i; j > lo && compar(a + (j - 1u) * width, a + j * width) > 0; j--)
            tkl_qswap(a + (j - 1u) * width, a + j * width, width);
}

#define TKL_QSORT_SMALL 12u
#define TKL_QSORT_SLOTS 64u

/* Iterative, and deliberately so.  The ranges still to order are held in two
 * arrays rather than in the call frames, and the larger half is always the one
 * saved, so the number of saved ranges never exceeds log2(nel) -- at most 32 for
 * any size_t this kernel addresses, against the 64 slots below.  A recursive
 * quicksort on a kernel stack would have to be given a depth limit to survive
 * the same input, and a limit that fires loses elements. */
__attribute__((weak)) void tkl_qsort(void *base, size_t nel, size_t width,
                                     int (*compar)(const void *, const void *))
{
    unsigned char *a = (unsigned char *)base;
    size_t lo_s[TKL_QSORT_SLOTS], hi_s[TKL_QSORT_SLOTS];
    unsigned int sp = 0u;
    size_t lo = 0u, hi = nel;            /* hi is exclusive */

    if (a == NULL || compar == NULL || width == 0u || nel < 2u) return;

    for (;;) {
        size_t mid, last, i, j, pivot_at;

        if (hi - lo <= TKL_QSORT_SMALL) {
            tkl_qinsert(a, lo, hi, width, compar);
            if (sp == 0u) return;
            lo = lo_s[--sp];
            hi = hi_s[sp];
            continue;
        }

        /* Median of three, then the pivot is moved to `lo` where the partition
         * can leave it until its final place is known.  The middle element is
         * the pivot rather than the first because a file listing arrives in
         * name order more often than not, and a first-element pivot makes that
         * case quadratic -- which is the same input this loop exists to survive. */
        mid  = lo + (hi - lo) / 2u;
        last = hi - 1u;
        if (compar(a + mid  * width, a + lo   * width) < 0)
            tkl_qswap(a + mid * width, a + lo * width, width);
        if (compar(a + last * width, a + lo   * width) < 0)
            tkl_qswap(a + last * width, a + lo * width, width);
        if (compar(a + mid  * width, a + last * width) < 0)
            tkl_qswap(a + mid * width, a + last * width, width);
        tkl_qswap(a + lo * width, a + mid * width, width);
        pivot_at = lo;

        i = lo + 1u;
        j = hi;
        for (;;) {
            while (i < j && compar(a + i * width, a + pivot_at * width) <= 0) i++;
            do { j--; } while (j > lo && compar(a + j * width, a + pivot_at * width) > 0);
            if (i >= j) break;
            tkl_qswap(a + i * width, a + j * width, width);
        }
        tkl_qswap(a + pivot_at * width, a + j * width, width);

        if (j - lo > hi - (j + 1u)) {
            lo_s[sp] = j + 1u; hi_s[sp] = hi; sp++;
            hi = j;
        } else {
            lo_s[sp] = lo;     hi_s[sp] = j;  sp++;
            lo = j + 1u;
        }
    }
}

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
void qsort(void *base, size_t nel, size_t width,
           int (*compar)(const void *, const void *)) __attribute__((weak, alias("tkl_qsort")));
#endif
