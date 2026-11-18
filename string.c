#include "string.h"
#include "stddef.h"
#include <stdarg.h>
void *memset(void*s,int v,u32 n){u8*p=s;while(n--)*p++=v;return s;}
void *memcpy(void*d,const void*s,u32 n){u8*D=d;const u8*S=s;while(n--)*D++=*S++;return d;}
u32 strlen(const char* s){u32 L=0;while(s[L])L++;return L;}
// NEW: strchr
char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return NULL;
}

// NEW: strstr
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

// NEW: atoi
int atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}
void *memmove(void *dest, const void *src, size_t n){
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    if (d == s || n == 0) return dest;
    if (d < s){
        for (size_t i=0;i<n;i++) d[i] = s[i];
    }else{
        for (size_t i=n; i>0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

static void snprintf_putc(char *buf, size_t size, size_t *i, char c) {
    if (*i + 1 < size) buf[*i] = c;
    (*i)++;
}

static void snprintf_puts(char *buf, size_t size, size_t *i, const char *s) {
    if (!s) s = "(null)";
    while (*s) snprintf_putc(buf, size, i, *s++);
}

static void snprintf_put_uint(char *buf, size_t size, size_t *i,
                              unsigned int value, unsigned int base,
                              int min_width, int zero_pad) {
    char tmp[32];
    int n = 0;
    const char *digits = "0123456789abcdef";

    if (base < 2 || base > 16) base = 10;
    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value && n < (int)sizeof(tmp));

    while (n < min_width) {
        snprintf_putc(buf, size, i, zero_pad ? '0' : ' ');
        min_width--;
    }
    while (n > 0) snprintf_putc(buf, size, i, tmp[--n]);
}

// ensure our snprintf symbol is available for mbedTLS build
int snprintf(char *buf, size_t size, const char *fmt, ...){
    if (!buf || !size) return 0;
    va_list ap; va_start(ap, fmt);
    size_t i = 0;

    for (const char *p = fmt; *p; ++p){
        if (*p != '%') {
            snprintf_putc(buf, size, &i, *p);
            continue;
        }

        p++;
        if (!*p) break;
        if (*p == '%') {
            snprintf_putc(buf, size, &i, '%');
            continue;
        }

        int zero_pad = 0;
        int min_width = 0;
        if (*p == '0') {
            zero_pad = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            min_width = min_width * 10 + (*p - '0');
            p++;
        }

        switch (*p) {
            case 's':
                snprintf_puts(buf, size, &i, va_arg(ap, const char *));
                break;
            case 'd': {
                int v = va_arg(ap, int);
                if (v < 0) {
                    snprintf_putc(buf, size, &i, '-');
                    v = -v;
                }
                snprintf_put_uint(buf, size, &i, (unsigned int)v, 10, min_width, zero_pad);
                break;
            }
            case 'u':
                snprintf_put_uint(buf, size, &i, va_arg(ap, unsigned int), 10, min_width, zero_pad);
                break;
            case 'x':
            case 'X':
                snprintf_put_uint(buf, size, &i, va_arg(ap, unsigned int), 16, min_width, zero_pad);
                break;
            default:
                snprintf_putc(buf, size, &i, '%');
                snprintf_putc(buf, size, &i, *p);
                break;
        }
    }

    buf[(i < size) ? i : size - 1] = '\0';
    va_end(ap);
    return (int)i;
}

// NEW: strcmp
int strcmp(const char *a, const char *b){
    while (*a && *b && *a == *b){ a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// NEW: strncmp
int strncmp(const char *a, const char *b, size_t n){
    size_t i=0;
    while (i < n && *a && *b && *a == *b){ a++; b++; i++; }
    if (i == n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

// NEW: strncpy (simple)
char *strncpy(char *dst, const char *src, size_t n){
    size_t i=0;
    for (; i+1 < n && src[i]; ++i) dst[i] = src[i];
    if (n>0) dst[i] = '\0';
    return dst;
}
