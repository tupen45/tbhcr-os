#ifndef TLS_H
#define TLS_H

#include <stdint.h>

// Perform an HTTPS GET to the given IPv4 address (host-order packed uint32)
// Returns number of body bytes written to out (<= out_cap), or negative on error.
int tls_http_get_by_ip(uint32_t ip, const char *host_header, const char *path, char *out, int out_cap);

int tls_http_request_by_ip(uint32_t ip, const char *method, const char *host_header,
                           const char *path, const char *bearer_token,
                           const char *json_body, char *out, int out_cap);

#endif // TLS_H
