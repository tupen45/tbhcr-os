/* Minimal platform hooks for mbedTLS in a freestanding kernel
 * Provides implementations for mbedtls_platform_zeroize and
 * mbedtls_platform_set_calloc_free registration helpers.
 */

#ifndef MBEDTLS_PLATFORM_STUBS_H
#define MBEDTLS_PLATFORM_STUBS_H

#include <stddef.h>

/* zeroize using volatile memset pointer trick (same as platform_util.c) */
static void * (* const volatile _memset_func_stub)( void *, int, size_t ) = (void*)0;

static void mbedtls_platform_zeroize_stub(void *buf, size_t len) {
    /* Fallback simple loop if memset is not available */
    volatile unsigned char *p = (volatile unsigned char*)buf;
    while (len--) *p++ = 0;
}

/* Provide dummy set_calloc_free to accept registration calls */
static int mbedtls_platform_set_calloc_free_stub(void * (*calloc_func)( size_t, size_t ), void (*free_func)( void * )) {
    (void)calloc_func; (void)free_func; return 0;
}

/* Make symbols visible to linker by declaring them (definitions provided by platform.c) */
void mbedtls_platform_zeroize(void *buf, size_t len);
int mbedtls_platform_set_calloc_free(void * (*calloc_func)( size_t, size_t ), void (*free_func)( void * ));

#endif
