#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

#include "mbedtls/platform.h"

/* Register the kernel allocator (calloc/free) with mbedTLS at init time. */
extern int mbedtls_platform_set_calloc_free( void * (*calloc_func)( size_t, size_t ), void (*free_func)( void * ) );

__attribute__((constructor)) static void platform_shim_init(void){
    /* register the kernel allocator with mbedTLS so modules using mbedtls_calloc/free use the kernel's calloc/free */
    mbedtls_platform_set_calloc_free(calloc, free);
}

/* Minimal vsnprintf supporting common specifiers used by mbedTLS debug prints. */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap){
    if(!buf || size==0) return 0;
    size_t i = 0;
    for(const char *p = fmt; *p && i < size-1; ++p){
        if(*p == '%'){
            p++;
            if(*p == 's'){
                const char *s = va_arg(ap, const char*);
                if(!s) s = "(null)";
                while(*s && i < size-1) buf[i++] = *s++;
            } else if(*p == 'd' || *p == 'i'){
                int v = va_arg(ap, int);
                char tmp[32]; int n=0; int neg=0;
                if(v==0) tmp[n++]='0';
                if(v<0){ neg=1; v=-v; }
                while(v>0 && n < (int)sizeof(tmp)-1){ tmp[n++] = '0' + (v%10); v/=10; }
                if(neg) tmp[n++]='-';
                for(int k = n-1; k>=0; --k) if(i < size-1) buf[i++] = tmp[k];
            } else if(*p == 'u'){
                unsigned int v = va_arg(ap, unsigned int);
                char tmp[32]; int n=0;
                if(v==0) tmp[n++]='0';
                while(v>0 && n < (int)sizeof(tmp)-1){ tmp[n++] = '0' + (v%10); v/=10; }
                for(int k = n-1; k>=0; --k) if(i < size-1) buf[i++] = tmp[k];
            } else if(*p == 'x' || *p == 'X'){
                unsigned int v = va_arg(ap, unsigned int);
                char tmp[32]; int n=0; const char *hex = "0123456789abcdef";
                if(v==0) tmp[n++]='0';
                while(v>0 && n < (int)sizeof(tmp)-1){ tmp[n++] = hex[v & 0xF]; v >>= 4; }
                for(int k = n-1; k>=0; --k) if(i < size-1) buf[i++] = tmp[k];
            } else if(*p == 'c'){
                char c = (char)va_arg(ap, int);
                if(i < size-1) buf[i++] = c;
            } else if(*p == '%'){
                if(i < size-1) buf[i++] = '%';
            } else {
                /* Unknown, copy verbatim */
                if(i < size-1) buf[i++] = *p;
            }
        } else {
            buf[i++] = *p;
        }
    }
    buf[i] = '\0';
    return (int)i;
}

/* Small runtime debug buffer for mbedTLS messages. Kernel UI can read via accessor. */
static char mbedtls_debug_buf[2048];
static size_t mbedtls_debug_len = 0;

const char *mbedtls_get_debug(void){
    return mbedtls_debug_buf;
}

/* Append formatted debug text into the buffer. Returns number of bytes written (like printf). */
int mbedtls_printf(const char *fmt, ...){
    if (!fmt) return 0;
    va_list ap; va_start(ap, fmt);
    size_t remaining = sizeof(mbedtls_debug_buf) - mbedtls_debug_len - 1;
    if (remaining == 0){ va_end(ap); return 0; }
    int n = vsnprintf(mbedtls_debug_buf + mbedtls_debug_len, remaining + 1, fmt, ap);
    if (n > 0){
        if ((size_t)n > remaining) n = (int)remaining;
        mbedtls_debug_len += (size_t)n;
        mbedtls_debug_buf[mbedtls_debug_len] = '\0';
    }
    va_end(ap);
    return n;
}
