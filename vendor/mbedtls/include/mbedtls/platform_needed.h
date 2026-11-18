/* Platform-needed common includes for a freestanding build
 * This header is force-included for mbedTLS sources to provide
 * prototypes and macros expected by vendor files (calloc/free/memcmp/INT_MAX etc).
 * It intentionally includes the project's freestanding headers when present.
 */

#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* If SIZE_MAX/INT_MAX not defined due to freestanding stub, provide conservative fallbacks */
#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* Provide memcmp mapping if not present in freestanding string.c */
#ifndef HAVE_MEMCMP
static inline int platform_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *A = a, *B = b; size_t i; for (i=0;i<n;i++) { if (A[i]!=B[i]) return (A[i]<B[i])?-1:1; } return 0;
}
#define memcmp platform_memcmp
#endif

/* Provide exit stub if not provided */
#ifndef HAVE_EXIT
static inline void platform_exit(int status){(void)status;}
#define exit platform_exit
#endif
