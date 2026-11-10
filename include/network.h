#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/* Funzioni di rete robuste */
int setup_server_socket(int port, int backlog);
int connect_to_server(const char *host, int port);

ssize_t send_all(int sock_fd, const void *buf, size_t len);
int send_line(int sock_fd, const char *fmt, ...);
int recv_line(int sock_fd, char *buf, size_t max_len);

#endif /* NETWORK_H */
