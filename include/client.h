#ifndef CLIENT_H
#define CLIENT_H

#include "protocol.h"
#include "board.h"
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

extern volatile sig_atomic_t g_client_running;
extern int g_sock_fd;
extern char g_last_server_event[BUFFER_SIZE];

enum GameSessionResult {
    SESSION_ERROR = 0,
    SESSION_LOBBY = 1,
    SESSION_WAIT_NEW_CHALLENGER = 2
};

/* Wrapper di lettura e multiplexing I/O */
int client_read_server_line(int sock, char *buf, size_t size);
int client_safe_read_line_multiplex(int sock, char *buf, size_t size);
int client_get_valid_int_multiplex(int sock, const char *prompt, int min, int max, int *out_val);
int client_get_valid_coords_multiplex(int sock, const char *prompt, int *out_row, int *out_col);

/* Modulo di gestione delle fasi di gioco (client_game.c) */
int run_game_session(int sock, const char *opponent_nick, bool start_my_turn, bool is_host);

/* Modulo di gestione della stanza e matchmaking (client_lobby.c) */
void host_wait_for_challenger(int sock);
void list_and_join_game(int sock);

#endif /* CLIENT_H */
