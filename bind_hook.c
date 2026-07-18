#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Original bind function pointer */
static int (*real_bind)(int, const struct sockaddr *, socklen_t) = NULL;

/* Target port - default 53, override via BIND_HOOK_PORT env */
static uint16_t target_port = 53;

/* Get the real bind function */
static void init_real_bind(void) {
    if (!real_bind) {
        real_bind = dlsym(RTLD_NEXT, "bind");
        if (!real_bind) {
            fprintf(stderr, "[bind_hook] FATAL: cannot find real bind()\n");
            _exit(1);
        }
    }
}

/* Get target port from environment */
static uint16_t get_target_port(void) {
    const char *env = getenv("BIND_HOOK_PORT");
    if (env) {
        int p = atoi(env);
        if (p > 0 && p < 65536)
            return (uint16_t)p;
    }
    return 53;
}

/* Extract port from sockaddr */
static uint16_t extract_port(const struct sockaddr *addr) {
    if (addr->sa_family == AF_INET) {
        return ntohs(((struct sockaddr_in *)addr)->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
    }
    return 0;
}

/* Our hook bind() */
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int initialized = 0;
    if (!initialized) {
        init_real_bind();
        target_port = get_target_port();
        initialized = 1;
    }

    uint16_t port = extract_port(addr);
    int orig_errno = errno;

    if (port == target_port) {
        int opt = 1;

        /* Try to set SO_REUSEPORT before bind */
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            /* Non-fatal: log and continue */
            fprintf(stderr, "[bind_hook] SO_REUSEPORT failed on fd %d: %s\n",
                    sockfd, strerror(errno));
        }

        /* Also set SO_REUSEADDR for good measure */
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    /* Call real bind */
    int ret = real_bind(sockfd, addr, addrlen);

    if (ret < 0 && port == target_port) {
        fprintf(stderr, "[bind_hook] bind(%d, :%u) failed: %s\n",
                sockfd, port, strerror(errno));
    }

    /* Restore original errno if bind succeeded */
    if (ret == 0)
        errno = orig_errno;

    return ret;
}
