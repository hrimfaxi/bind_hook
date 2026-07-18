#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ip:port> <count>\n", argv[0]);
        return 1;
    }

    char *addr_str = argv[1];
    int count = atoi(argv[2]);

    char *colon = strrchr(addr_str, ':');
    if (!colon) {
        fprintf(stderr, "Invalid address format, expected ip:port\n");
        return 1;
    }

    *colon = '\0';
    const char *ip = addr_str;
    int port = atoi(colon + 1);

    int socks[16];
    if (count > 16) count = 16;

    for (int i = 0; i < count; i++) {
        int opt = 1;

        socks[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (socks[i] < 0) {
            perror("socket");
            return 1;
        }

        /* Enable IP_TRANSPARENT so we can bind to non-local addresses (tproxy) */
        if (setsockopt(socks[i], IPPROTO_IP, IP_TRANSPARENT, &opt, sizeof(opt)) < 0) {
            fprintf(stderr, "[bind_hook] IP_TRANSPARENT failed on fd %d: %s\n",
                socks[i], strerror(errno));
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);

        if (bind(socks[i], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            printf("[%d] bind(%s:%d) FAILED: %s\n", i, ip, port, strerror(errno));
        } else {
            printf("[%d] bind(%s:%d) OK\n", i, ip, port);
        }
    }

    /* Keep sockets open so they remain bound */
    printf("Press Enter to close sockets...\n");
    getchar();

    for (int i = 0; i < count; i++) {
        close(socks[i]);
    }

    return 0;
}
