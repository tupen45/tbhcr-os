// Minimal mbedTLS glue for freestanding kernel
// Provides: int tls_http_get_by_ip(uint32_t ip, const char *host_header, const char *path, char *out, int out_cap);

#include "tls.h"
#include "tcp.h"
#include "stdlib.h"
#include "string.h"
#include "io.h" // for debug prints if needed
#include "rtl8139.h"

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "vendor/mbedtls/include/mbedtls/platform_stubs.h"

#include <stdio.h>

/* Provide ssize_t for freestanding build */
typedef int ssize_t;
#include "vendor/mbedtls/include/mbedtls/net_sockets.h"

// Expose mbedTLS debug buffer accessor from platform_shim
extern const char *mbedtls_get_debug(void);

// No custom calloc/free here — use the kernel's malloc/free implementations (kmalloc_stub.c)

// Provide a simple time stub used by some parts of mbedTLS when macros expect it
long mbedtls_time_stub(long *t){ if(t) *t = 0; return 0; }

// mbedTLS debug callback: append module/level/message to platform buffer via mbedtls_printf
static void mbedtls_debug_cb(void *ctx, int level, const char *file, int line, const char *str){
    (void)ctx; (void)file; (void)line; (void)level;
    mbedtls_printf("%s", str);
}

// TCP send/recv wrappers used by mbedTLS
static int net_send(void *ctx, const unsigned char *buf, size_t len){
    tcp_socket_t *s = (tcp_socket_t*)ctx;
    int ret = tcp_send(s, (const char*)buf, (int)len);
    if(ret < 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret;
}

static int net_recv(void *ctx, unsigned char *buf, size_t len){
    tcp_socket_t *s = (tcp_socket_t*)ctx;
    // call tcp_recv which is non-blocking in this kernel design: 0 = nothing yet
    int ret = tcp_recv(s, (char*)buf, (int)len);
    if(ret == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    if(ret < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return ret;
}

int tls_http_get_by_ip(uint32_t ip, const char *host_header, const char *path, char *out, int out_cap){
    return tls_http_request_by_ip(ip, "GET", host_header, path, NULL, NULL, out, out_cap);
}

int tls_http_request_by_ip(uint32_t ip, const char *method, const char *host_header,
                           const char *path, const char *bearer_token,
                           const char *json_body, char *out, int out_cap){
    if(ip==0 || !out || out_cap <= 0) return -1;

    static int tls_alloc_inited = 0;
    if (!tls_alloc_inited) {
        mbedtls_platform_set_calloc_free(calloc, free);
        tls_alloc_inited = 1;
    }

    tcp_init();
    if(tcp_connect(&g_sock, ip, 443, 0) != 0){ return -10; }

    /* Wait for TCP 3-way handshake on g_sock before starting TLS */
    int tcp_wait = 0;
    while (g_sock.state != TCP_ESTABLISHED && tcp_wait < 300000) {
        net_poll();
        tcp_wait++;
    }
    if (g_sock.state != TCP_ESTABLISHED) {
        tcp_close(&g_sock);
        return -11;
    }

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;

    const char *pers = "mbed_tls_kern";
    mbedtls_ctr_drbg_init(&ctr_drbg);

    /* Lightweight internal entropy callback for freestanding testing. */
    static uint32_t lfsr = 0x12345678;
    int my_entropy(void *data, unsigned char *buf, size_t len){
        (void)data;
        for(size_t i=0;i<len;i++){
            lfsr ^= lfsr << 13;
            lfsr ^= lfsr >> 17;
            lfsr ^= lfsr << 5;
            buf[i] = (unsigned char)(lfsr & 0xFF);
        }
        return 0;
    }

    if(mbedtls_ctr_drbg_seed(&ctr_drbg, my_entropy, NULL, (const unsigned char*)pers, strlen(pers)) != 0){
        tcp_close(&g_sock); return -12; }

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    if(mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0){
        mbedtls_ssl_config_free(&conf); tcp_close(&g_sock); return -13; }

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_dbg(&conf, mbedtls_debug_cb, NULL);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    if(mbedtls_ssl_setup(&ssl, &conf) != 0){ mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); tcp_close(&g_sock); return -14; }

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if(host_header && host_header[0]){
        mbedtls_ssl_set_hostname(&ssl, host_header);
    }
#endif

    mbedtls_ssl_set_bio(&ssl, &g_sock, net_send, net_recv, NULL);

    int ret;
    while((ret = mbedtls_ssl_handshake(&ssl)) != 0){
        if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE){
            net_poll();
            continue;
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        tcp_close(&g_sock);
        return -15;
    }

    // Build HTTP request (GET / POST with optional Bearer token and JSON body)
    static char req[2048];
    int body_len_in = (json_body && json_body[0]) ? (int)strlen(json_body) : 0;
    int pos = snprintf(req, sizeof(req),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Accept: application/json\r\n"
                       "Connection: close\r\n",
                       (method && method[0]) ? method : "GET",
                       (path && path[0]) ? path : "/",
                       host_header ? host_header : "");
    if (bearer_token && bearer_token[0] && pos > 0 && pos < (int)sizeof(req) - 64) {
        pos += snprintf(req + pos, sizeof(req) - pos, "Authorization: Bearer %s\r\n", bearer_token);
    }
    if (body_len_in > 0 && pos > 0 && pos < (int)sizeof(req) - 64) {
        pos += snprintf(req + pos, sizeof(req) - pos,
                        "Content-Type: application/json\r\nContent-Length: %d\r\n",
                        body_len_in);
    }
    if (pos > 0 && pos < (int)sizeof(req) - 4) {
        pos += snprintf(req + pos, sizeof(req) - pos, "\r\n");
    }
    if (body_len_in > 0 && pos > 0 && pos + body_len_in < (int)sizeof(req)) {
        memcpy(req + pos, json_body, body_len_in);
        pos += body_len_in;
        req[pos] = '\0';
    }
    int n = pos;
    if(n <= 0){ goto cleanup_err; }

    // write request
    int w = mbedtls_ssl_write(&ssl, (unsigned char*)req, n);
    if(w <= 0) goto cleanup_err;

    // read response into temporary buffer then strip HTTP headers
    static char tls_raw[16384];
    int total = 0;
    while(total < (int)sizeof(tls_raw) - 1){
        int r = mbedtls_ssl_read(&ssl, (unsigned char*)(tls_raw + total), sizeof(tls_raw) - 1 - total);
        if(r > 0){ total += r; continue; }
        if(r == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_WRITE){
            net_poll();
            continue;
        }
        break; // EOF or error
    }
    tls_raw[total] = '\0';

    char *body = strstr(tls_raw, "\r\n\r\n");
    if (body) body += 4; else body = tls_raw;
    int body_len = (int)strlen(body);
    if (body_len > out_cap - 1) body_len = out_cap - 1;
    if (body_len > 0) memcpy(out, body, body_len);
    out[body_len] = '\0';

    // cleanup and return
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    tcp_close(&g_sock);
    return body_len;

cleanup_err:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    tcp_close(&g_sock);
    return -16;
}
