// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0


#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "vm.h"
#include "gc.h"
#include "error.h"
#include "gc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>


#define NET_MAX_TLS_CONNS  64
#define NET_INVALID_ID     -1

typedef struct {
    int      active;
    int      fd;
    SSL     *ssl;
    SSL_CTX *ctx;       
    int      is_server; 
} TlsConn;

static TlsConn  tls_pool[NET_MAX_TLS_CONNS];
static int      net_initialized = 0;

static void net_init(void) {
    if (net_initialized) return;
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    memset(tls_pool, 0, sizeof(tls_pool));
    net_initialized = 1;
}

static int tls_alloc(void) {
    for (int i = 0; i < NET_MAX_TLS_CONNS; i++) {
        if (!tls_pool[i].active) return i;
    }
    return NET_INVALID_ID;
}

static TlsConn *tls_get(int id) {
    if (id < 0 || id >= NET_MAX_TLS_CONNS) return NULL;
    if (!tls_pool[id].active) return NULL;
    return &tls_pool[id];
}


#define N_PUSH(v)  do { vm->stack[vm->stack_top++] = (v); } while(0)
#define N_POP()    (vm->stack[--vm->stack_top])
#define STR(v)     (IS_STRING(v) ? AS_STRING(v)->chars : "")
#define NUM(v)     (IS_NUMBER(v) ? (int)AS_NUMBER(v) : 0)
#define NUMF(v)    (IS_NUMBER(v) ? AS_NUMBER(v) : 0.0)

static Value sv(const char *s) {
    return s ? STRING_VAL(gc_cstring(s)) : NIL_VAL;
}


static Value arr3(Value a, Value b, Value c) {
    ObjArray *arr = gc_array();
    gc_arr_push(arr, a);
    gc_arr_push(arr, b);
    gc_arr_push(arr, c);
    return ARRAY_VAL(arr);
}


static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}


static int wait_fd(int fd, int write, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    return select(fd + 1,
                  write ? NULL : &fds,
                  write ? &fds : NULL,
                  NULL, tvp);
}


static int resolve_host(const char *host, struct sockaddr_in *out) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -1;
    memcpy(out, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    return 0;
}


typedef struct {
    int   status;
    char *headers;  
    char *body;     
    int   body_len;
} HttpResp;


static int parse_url(const char *url,
                     char *host, int hlen,
                     int  *port,
                     char *path, int plen,
                     int  *use_tls) {
    const char *p = url;
    *use_tls = 0;
    *port    = 80;
    if (strncmp(p, "https://", 8) == 0) { *use_tls = 1; *port = 443; p += 8; }
    else if (strncmp(p, "http://", 7) == 0)  { p += 7; }
    else return -1;

    
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    int host_end = slash ? (int)(slash - p) : (int)strlen(p);
    if (colon && (!slash || colon < slash)) {
        int hlen2 = (int)(colon - p);
        if (hlen2 >= hlen) hlen2 = hlen - 1;
        memcpy(host, p, hlen2); host[hlen2] = '\0';
        *port = atoi(colon + 1);
    } else {
        if (host_end >= hlen) host_end = hlen - 1;
        memcpy(host, p, host_end); host[host_end] = '\0';
    }
    
    if (slash) { strncpy(path, slash, plen - 1); path[plen-1] = '\0'; }
    else        { strncpy(path, "/", plen - 1); }
    return 0;
}

#define HTTP_RBUF  (1 << 20)  

static void free_resp(HttpResp *r) {
    free(r->headers); r->headers = NULL;
    free(r->body);    r->body    = NULL;
}


static int http_exchange(int fd, const char *request, HttpResp *resp, int timeout_ms) {
    resp->status   = 0;
    resp->headers  = NULL;
    resp->body     = NULL;
    resp->body_len = 0;

    
    int rlen = (int)strlen(request);
    int sent = 0;
    while (sent < rlen) {
        if (wait_fd(fd, 1, timeout_ms) <= 0) return -1;
        int n = (int)send(fd, request + sent, rlen - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += n;
    }

    
    char *buf = (char*)malloc(HTTP_RBUF);
    if (!buf) return -1;
    int total = 0;

    while (1) {
        if (wait_fd(fd, 0, timeout_ms) <= 0) break;
        int n = (int)recv(fd, buf + total, HTTP_RBUF - total - 1, 0);
        if (n <= 0) break;
        total += n;
        if (total >= HTTP_RBUF - 1) break;
    }
    buf[total] = '\0';
    if (total == 0) { free(buf); return -1; }

    
    char *nl = strchr(buf, '\n');
    if (!nl) { free(buf); return -1; }
    
    const char *sp = strchr(buf, ' ');
    if (sp && sp < nl) resp->status = atoi(sp + 1);

    
    char *body_start = strstr(buf, "\r\n\r\n");
    int   hdr_end    = 4;
    if (!body_start) { body_start = strstr(buf, "\n\n"); hdr_end = 2; }
    if (!body_start) { body_start = buf + total; hdr_end = 0; }

    int hdr_len = (int)(body_start - buf);
    resp->headers = (char*)malloc(hdr_len + 1);
    if (resp->headers) { memcpy(resp->headers, buf, hdr_len); resp->headers[hdr_len] = '\0'; }

    char *body_ptr = body_start + hdr_end;
    int   body_len = total - (int)(body_ptr - buf);
    if (body_len > 0) {
        resp->body = (char*)malloc(body_len + 1);
        if (resp->body) { memcpy(resp->body, body_ptr, body_len); resp->body[body_len] = '\0'; }
        resp->body_len = body_len;
    }

    free(buf);
    return 0;
}


static int https_exchange(SSL *ssl, const char *request, HttpResp *resp) {
    resp->status   = 0;
    resp->headers  = NULL;
    resp->body     = NULL;
    resp->body_len = 0;

    int rlen = (int)strlen(request);
    int sent = 0;
    while (sent < rlen) {
        int n = SSL_write(ssl, request + sent, rlen - sent);
        if (n <= 0) return -1;
        sent += n;
    }

    char *buf = (char*)malloc(HTTP_RBUF);
    if (!buf) return -1;
    int total = 0;

    while (1) {
        int n = SSL_read(ssl, buf + total, HTTP_RBUF - total - 1);
        if (n <= 0) break;
        total += n;
        if (total >= HTTP_RBUF - 1) break;
    }
    buf[total] = '\0';

    char *sp = strchr(buf, ' ');
    if (sp) resp->status = atoi(sp + 1);

    char *body_start = strstr(buf, "\r\n\r\n");
    int   hdr_end    = 4;
    if (!body_start) { body_start = strstr(buf, "\n\n"); hdr_end = 2; }
    if (!body_start) { body_start = buf + total; hdr_end = 0; }

    int hdr_len = (int)(body_start - buf);
    resp->headers = (char*)malloc(hdr_len + 1);
    if (resp->headers) { memcpy(resp->headers, buf, hdr_len); resp->headers[hdr_len] = '\0'; }

    char *body_ptr = body_start + hdr_end;
    int   body_len = total - (int)(body_ptr - buf);
    if (body_len > 0) {
        resp->body = (char*)malloc(body_len + 1);
        if (resp->body) { memcpy(resp->body, body_ptr, body_len); resp->body[body_len] = '\0'; }
        resp->body_len = body_len;
    }
    free(buf);
    return 0;
}


void net_dispatch(VM *vm, uint16_t id, uint8_t argc, Value *args) {
    net_init();
    if (argc > 8) argc = 8;  

    switch (id) {

    
    case NATIVE_NET_TCP_LISTEN: {
        int port = argc >= 1 ? NUM(args[0]) : 8080;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            listen(fd, 16) < 0) {
            close(fd); N_PUSH(NUMBER_VAL(-1)); break;
        }
        N_PUSH(NUMBER_VAL((double)fd));
        break;
    }

    
    case NATIVE_NET_TCP_ACCEPT: {
        int fd  = argc >= 1 ? NUM(args[0]) : -1;
        int tms = argc >= 2 ? NUM(args[1]) : 5000;
        if (fd < 0) { N_PUSH(NIL_VAL); break; }
        if (wait_fd(fd, 0, tms) <= 0) { N_PUSH(NIL_VAL); break; }
        struct sockaddr_in peer = {0};
        socklen_t plen = sizeof(peer);
        int client = accept(fd, (struct sockaddr*)&peer, &plen);
        if (client < 0) { N_PUSH(NIL_VAL); break; }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        N_PUSH(arr3(NUMBER_VAL((double)client), sv(ip),
                    NUMBER_VAL((double)ntohs(peer.sin_port))));
        break;
    }

    
    case NATIVE_NET_TCP_CONNECT: {
        const char *host = argc >= 1 ? STR(args[0]) : "";
        int port  = argc >= 2 ? NUM(args[1]) : 80;
        int tms   = argc >= 3 ? NUM(args[2]) : 5000;

        struct sockaddr_in addr = {0};
        if (resolve_host(host, &addr) < 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        addr.sin_port = htons((uint16_t)port);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { N_PUSH(NUMBER_VAL(-1)); break; }

        set_nonblocking(fd);
        int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (r < 0 && errno != EINPROGRESS) { close(fd); N_PUSH(NUMBER_VAL(-1)); break; }
        if (wait_fd(fd, 1, tms) <= 0) { close(fd); N_PUSH(NUMBER_VAL(-1)); break; }
        
        int err = 0; socklen_t elen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) { close(fd); N_PUSH(NUMBER_VAL(-1)); break; }
        set_blocking(fd);
        N_PUSH(NUMBER_VAL((double)fd));
        break;
    }

    
    case NATIVE_NET_SEND: {
        int fd         = argc >= 1 ? NUM(args[0]) : -1;
        const char *d  = argc >= 2 ? STR(args[1]) : "";
        if (fd < 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        int n = (int)send(fd, d, strlen(d), MSG_NOSIGNAL);
        N_PUSH(NUMBER_VAL((double)n));
        break;
    }

    
    case NATIVE_NET_RECV: {
        int fd  = argc >= 1 ? NUM(args[0]) : -1;
        int mx  = argc >= 2 ? NUM(args[1]) : 4096;
        int tms = argc >= 3 ? NUM(args[2]) : 5000;
        if (fd < 0 || mx <= 0) { N_PUSH(NIL_VAL); break; }
        if (wait_fd(fd, 0, tms) <= 0) { N_PUSH(NIL_VAL); break; }
        char *buf = (char*)malloc(mx + 1);
        if (!buf) { N_PUSH(NIL_VAL); break; }
        int n = (int)recv(fd, buf, mx, 0);
        if (n <= 0) { free(buf); N_PUSH(NIL_VAL); break; }
        buf[n] = '\0';
        N_PUSH(sv(buf));
        free(buf);
        break;
    }

    
    case NATIVE_NET_CLOSE: {
        int fd = argc >= 1 ? NUM(args[0]) : -1;
        if (fd >= 0) close(fd);
        N_PUSH(NIL_VAL);
        break;
    }

    
    case NATIVE_NET_DNS: {
        const char *host = argc >= 1 ? STR(args[0]) : "";
        struct sockaddr_in addr = {0};
        if (resolve_host(host, &addr) < 0) { N_PUSH(NIL_VAL); break; }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        N_PUSH(sv(ip));
        break;
    }

    
    case NATIVE_NET_UDP_BIND: {
        int port = argc >= 1 ? NUM(args[0]) : 0;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd); N_PUSH(NUMBER_VAL(-1)); break;
        }
        N_PUSH(NUMBER_VAL((double)fd));
        break;
    }

    
    case NATIVE_NET_UDP_SEND: {
        int fd         = argc >= 1 ? NUM(args[0]) : -1;
        const char *h  = argc >= 2 ? STR(args[1]) : "";
        int port       = argc >= 3 ? NUM(args[2]) : 0;
        const char *d  = argc >= 4 ? STR(args[3]) : "";
        if (fd < 0) { N_PUSH(BOOL_VAL(false)); break; }
        struct sockaddr_in addr = {0};
        if (resolve_host(h, &addr) < 0) { N_PUSH(BOOL_VAL(false)); break; }
        addr.sin_port = htons((uint16_t)port);
        int n = (int)sendto(fd, d, strlen(d), 0,
                            (struct sockaddr*)&addr, sizeof(addr));
        N_PUSH(BOOL_VAL(n > 0));
        break;
    }

    
    case NATIVE_NET_UDP_RECV: {
        int fd  = argc >= 1 ? NUM(args[0]) : -1;
        int mx  = argc >= 2 ? NUM(args[1]) : 4096;
        int tms = argc >= 3 ? NUM(args[2]) : 2000;
        if (fd < 0) { N_PUSH(NIL_VAL); break; }
        if (wait_fd(fd, 0, tms) <= 0) { N_PUSH(NIL_VAL); break; }
        char *buf = (char*)malloc(mx + 1);
        struct sockaddr_in peer = {0}; socklen_t plen = sizeof(peer);
        if (!buf) { N_PUSH(NIL_VAL); break; }
        int n = (int)recvfrom(fd, buf, mx, 0, (struct sockaddr*)&peer, &plen);
        if (n < 0) { free(buf); N_PUSH(NIL_VAL); break; }
        buf[n] = '\0';
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        N_PUSH(arr3(sv(buf), sv(ip), NUMBER_VAL((double)ntohs(peer.sin_port))));
        free(buf);
        break;
    }

    
    case NATIVE_NET_HTTP_GET: {
        const char *url  = argc >= 1 ? STR(args[0]) : "";
        const char *xhdr = argc >= 2 ? STR(args[1]) : "";
        int tms          = argc >= 3 ? NUM(args[2]) : 10000;

        char host[512], path[1024];
        int  port, use_tls;
        if (parse_url(url, host, sizeof(host), &port, path, sizeof(path), &use_tls) < 0) {
            N_PUSH(arr3(NUMBER_VAL(-1), sv("bad url"), sv(""))); break;
        }

        char req[2048];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n",
                 path, host, xhdr);

        HttpResp resp = {0};
        int ok = -1;

        if (!use_tls) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr = {0};
            if (fd >= 0 && resolve_host(host, &addr) == 0) {
                addr.sin_port = htons((uint16_t)port);
                set_nonblocking(fd);
                connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (wait_fd(fd, 1, tms) > 0) {
                    set_blocking(fd);
                    ok = http_exchange(fd, req, &resp, tms);
                }
                close(fd);
            } else if (fd >= 0) close(fd);
        } else {
            SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_default_verify_paths(ctx);
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr = {0};
            if (fd >= 0 && resolve_host(host, &addr) == 0) {
                addr.sin_port = htons((uint16_t)port);
                set_nonblocking(fd);
                connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (wait_fd(fd, 1, tms) > 0) {
                    set_blocking(fd);
                    SSL *ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, fd);
                    SSL_set_tlsext_host_name(ssl, host);
                    if (SSL_connect(ssl) == 1) ok = https_exchange(ssl, req, &resp);
                    SSL_shutdown(ssl); SSL_free(ssl);
                }
                close(fd);
            } else if (fd >= 0) close(fd);
            SSL_CTX_free(ctx);
        }

        if (ok < 0) {
            N_PUSH(arr3(NUMBER_VAL(-1), sv("connection failed"), sv("")));
        } else {
            Value body = resp.body ? sv(resp.body) : sv("");
            Value hdrs = resp.headers ? sv(resp.headers) : sv("");
            N_PUSH(arr3(NUMBER_VAL((double)resp.status), hdrs, body));
        }
        free_resp(&resp);
        break;
    }

    
    case NATIVE_NET_HTTP_POST: {
        const char *url  = argc >= 1 ? STR(args[0]) : "";
        const char *body = argc >= 2 ? STR(args[1]) : "";
        const char *ct   = argc >= 3 ? STR(args[2]) : "application/x-www-form-urlencoded";
        int tms          = argc >= 4 ? NUM(args[3]) : 10000;

        char host[512], path[1024];
        int  port, use_tls;
        if (parse_url(url, host, sizeof(host), &port, path, sizeof(path), &use_tls) < 0) {
            N_PUSH(arr3(NUMBER_VAL(-1), sv("bad url"), sv(""))); break;
        }

        char req[4096];
        snprintf(req, sizeof(req),
                 "POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: %s\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                 path, host, ct, (int)strlen(body), body);

        HttpResp resp = {0};
        int ok = -1;

        if (!use_tls) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr = {0};
            if (fd >= 0 && resolve_host(host, &addr) == 0) {
                addr.sin_port = htons((uint16_t)port);
                set_nonblocking(fd);
                connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (wait_fd(fd, 1, tms) > 0) {
                    set_blocking(fd);
                    ok = http_exchange(fd, req, &resp, tms);
                }
                close(fd);
            } else if (fd >= 0) close(fd);
        } else {
            SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_default_verify_paths(ctx);
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr = {0};
            if (fd >= 0 && resolve_host(host, &addr) == 0) {
                addr.sin_port = htons((uint16_t)port);
                set_nonblocking(fd); connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (wait_fd(fd, 1, tms) > 0) {
                    set_blocking(fd);
                    SSL *ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, fd);
                    SSL_set_tlsext_host_name(ssl, host);
                    if (SSL_connect(ssl) == 1) ok = https_exchange(ssl, req, &resp);
                    SSL_shutdown(ssl); SSL_free(ssl);
                }
                close(fd);
            } else if (fd >= 0) close(fd);
            SSL_CTX_free(ctx);
        }

        if (ok < 0) N_PUSH(arr3(NUMBER_VAL(-1), sv("connection failed"), sv("")));
        else        N_PUSH(arr3(NUMBER_VAL((double)resp.status),
                                resp.headers ? sv(resp.headers) : sv(""),
                                resp.body    ? sv(resp.body)    : sv("")));
        free_resp(&resp);
        break;
    }

    
    case NATIVE_NET_TLS_LISTEN: {
        int port        = argc >= 1 ? NUM(args[0]) : 8443;
        const char *crt = argc >= 2 ? STR(args[1]) : "";
        const char *key = argc >= 3 ? STR(args[2]) : "";
        const char *ca  = argc >= 4 ? STR(args[3]) : "";

        int id = tls_alloc();
        if (id == NET_INVALID_ID) { N_PUSH(NUMBER_VAL(-1)); break; }

        SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
        if (!ctx) { N_PUSH(NUMBER_VAL(-1)); break; }

        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        if (*ca) SSL_CTX_load_verify_locations(ctx, ca, NULL);

        if (SSL_CTX_use_certificate_file(ctx, crt, SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)  != 1) {
            SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break; }
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            listen(fd, 16) < 0) {
            close(fd); SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break;
        }

        tls_pool[id].active    = 1;
        tls_pool[id].fd        = fd;
        tls_pool[id].ssl       = NULL;
        tls_pool[id].ctx       = ctx;
        tls_pool[id].is_server = 1;
        N_PUSH(NUMBER_VAL((double)id));
        break;
    }

    
    case NATIVE_NET_TLS_ACCEPT: {
        int lid = argc >= 1 ? NUM(args[0]) : -1;
        int tms = argc >= 2 ? NUM(args[1]) : 5000;
        TlsConn *listener = tls_get(lid);
        if (!listener || !listener->ctx) { N_PUSH(NUMBER_VAL(-1)); break; }

        if (wait_fd(listener->fd, 0, tms) <= 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        struct sockaddr_in peer = {0}; socklen_t plen = sizeof(peer);
        int cfd = accept(listener->fd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) { N_PUSH(NUMBER_VAL(-1)); break; }

        int cid = tls_alloc();
        if (cid == NET_INVALID_ID) { close(cfd); N_PUSH(NUMBER_VAL(-1)); break; }

        SSL *ssl = SSL_new(listener->ctx);
        SSL_set_fd(ssl, cfd);
        if (SSL_accept(ssl) != 1) {
            SSL_free(ssl); close(cfd); N_PUSH(NUMBER_VAL(-1)); break;
        }

        tls_pool[cid].active    = 1;
        tls_pool[cid].fd        = cfd;
        tls_pool[cid].ssl       = ssl;
        tls_pool[cid].ctx       = NULL;  
        tls_pool[cid].is_server = 1;
        N_PUSH(NUMBER_VAL((double)cid));
        break;
    }

    
    case NATIVE_NET_TLS_CONNECT: {
        const char *host = argc >= 1 ? STR(args[0]) : "";
        int port         = argc >= 2 ? NUM(args[1]) : 443;
        const char *ca   = argc >= 3 ? STR(args[2]) : "";
        int tms          = argc >= 4 ? NUM(args[3]) : 5000;

        int id = tls_alloc();
        if (id == NET_INVALID_ID) { N_PUSH(NUMBER_VAL(-1)); break; }

        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (*ca && *ca != 0)
            SSL_CTX_load_verify_locations(ctx, ca, NULL);
        else
            SSL_CTX_set_default_verify_paths(ctx);

        struct sockaddr_in addr = {0};
        if (resolve_host(host, &addr) < 0) {
            SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break;
        }
        addr.sin_port = htons((uint16_t)port);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break; }
        set_nonblocking(fd);
        connect(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (wait_fd(fd, 1, tms) <= 0) {
            close(fd); SSL_CTX_free(ctx); N_PUSH(NUMBER_VAL(-1)); break;
        }
        set_blocking(fd);

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host);
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl); close(fd); SSL_CTX_free(ctx);
            N_PUSH(NUMBER_VAL(-1)); break;
        }

        tls_pool[id].active    = 1;
        tls_pool[id].fd        = fd;
        tls_pool[id].ssl       = ssl;
        tls_pool[id].ctx       = ctx;
        tls_pool[id].is_server = 0;
        N_PUSH(NUMBER_VAL((double)id));
        break;
    }

    
    case NATIVE_NET_TLS_SEND: {
        int id         = argc >= 1 ? NUM(args[0]) : -1;
        const char *d  = argc >= 2 ? STR(args[1]) : "";
        TlsConn *c = tls_get(id);
        if (!c || !c->ssl) { N_PUSH(NUMBER_VAL(-1)); break; }
        int n = SSL_write(c->ssl, d, (int)strlen(d));
        N_PUSH(NUMBER_VAL((double)n));
        break;
    }

    
    case NATIVE_NET_TLS_RECV: {
        int id  = argc >= 1 ? NUM(args[0]) : -1;
        int mx  = argc >= 2 ? NUM(args[1]) : 4096;
        int tms = argc >= 3 ? NUM(args[2]) : 5000;
        TlsConn *c = tls_get(id);
        if (!c || !c->ssl || mx <= 0) { N_PUSH(NIL_VAL); break; }
        
        if (wait_fd(c->fd, 0, tms) <= 0 && !SSL_pending(c->ssl)) {
            N_PUSH(NIL_VAL); break;
        }
        char *buf = (char*)malloc(mx + 1);
        if (!buf) { N_PUSH(NIL_VAL); break; }
        int n = SSL_read(c->ssl, buf, mx);
        if (n <= 0) { free(buf); N_PUSH(NIL_VAL); break; }
        buf[n] = '\0';
        N_PUSH(sv(buf));
        free(buf);
        break;
    }

    
    case NATIVE_NET_TLS_CLOSE: {
        int id = argc >= 1 ? NUM(args[0]) : -1;
        TlsConn *c = tls_get(id);
        if (c) {
            if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
            if (c->ctx) SSL_CTX_free(c->ctx);
            close(c->fd);
            memset(c, 0, sizeof(*c));
        }
        N_PUSH(NIL_VAL);
        break;
    }

    
    case NATIVE_NET_PEER_ADDR: {
        int fd = argc >= 1 ? NUM(args[0]) : -1;
        if (fd < 0) { N_PUSH(sv("?")); break; }
        struct sockaddr_in peer = {0}; socklen_t plen = sizeof(peer);
        if (getpeername(fd, (struct sockaddr*)&peer, &plen) < 0) {
            N_PUSH(sv("?")); break;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s:%d", ip, (int)ntohs(peer.sin_port));
        N_PUSH(sv(buf));
        break;
    }

    
    case NATIVE_NET_SET_TIMEOUT: {
        int fd  = argc >= 1 ? NUM(args[0]) : -1;
        int tms = argc >= 2 ? NUM(args[1]) : 5000;
        if (fd >= 0) {
            struct timeval tv;
            tv.tv_sec  = tms / 1000;
            tv.tv_usec = (tms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        N_PUSH(NIL_VAL);
        break;
    }

    default:
        N_PUSH(NIL_VAL);
        break;
    }
}
