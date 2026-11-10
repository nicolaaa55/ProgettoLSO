#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "network.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int setup_server_socket(int port, int backlog) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_error("socket() fallita: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_warn("setsockopt(SO_REUSEADDR) fallita: %s", strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind() sulla porta %d fallita: %s", port, strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, backlog) < 0) {
        log_error("listen() fallita: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int connect_to_server(const char *host, int port) {
    if (!host || port <= 0 || port > 65535) {
        return -1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);

    /* Conversione indirizzo IP con inet_aton */
    if (inet_aton(host, &serv_addr.sin_addr) == 0) {
        /* Supporto per "localhost" o nomi host tramite gethostbyname */
        struct hostent *he = gethostbyname(host);
        if (!he) {
            log_error("Impossibile risolvere l'indirizzo host: %s", host);
            close(sock_fd);
            return -1;
        }
        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

ssize_t send_all(int sock_fd, const void *buf, size_t len) {
    const char *ptr = (const char *)buf;
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t n = send(sock_fd, ptr + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total_sent += (size_t)n;
    }

    return (ssize_t)total_sent;
}

int send_line(int sock_fd, const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer) - 2, fmt, args);
    va_end(args);

    if (written < 0) return -1;
    if ((size_t)written >= sizeof(buffer) - 2) {
        written = (int)(sizeof(buffer) - 3);
    }

    buffer[written] = '\n';
    buffer[written + 1] = '\0';

    ssize_t sent = send_all(sock_fd, buffer, (size_t)(written + 1));
    return (sent == written + 1) ? 0 : -1;
}

int recv_line(int sock_fd, char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;

    size_t count = 0;
    bool truncated = false;

    while (1) {
        char c;
        ssize_t n = read(sock_fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1; /* Errore di lettura o socket chiusa bruscamente */
        }
        if (n == 0) {
            /* Connessione chiusa dall'interlocutore */
            if (count == 0 && !truncated) return 0;
            break;
        }

        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue; /* Ignora carriage return */
        }

        if (count < max_len - 1) {
            buf[count++] = c;
        } else {
            truncated = true;
            /* Scarta i byte in eccesso fino al newline per non sporcare i comandi successivi */
        }
    }

    buf[count] = '\0';
    return (int)count;
}
