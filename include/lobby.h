#ifndef LOBBY_H
#define LOBBY_H

#include "protocol.h"
#include "game.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct ClientSession {
    int socket_fd;
    char nickname[MAX_NICK_LEN];
    ClientState state;
    int active_game_id;
    pthread_mutex_t send_lock;
    pthread_t thread_id;
    bool active;
} ClientSession;

void lobby_init(void);
void lobby_destroy(void);

/* Client management */
ClientSession *lobby_add_client(int socket_fd);
void lobby_remove_client(ClientSession *client);
int client_send(ClientSession *client, const char *fmt, ...);

/* Game management */
int lobby_create_game(ClientSession *host, const char *game_name, const char **err_msg);
Game *lobby_get_game(int game_id);
void lobby_send_games_list(ClientSession *client);
bool lobby_request_join(ClientSession *guest, int game_id, const char **err_msg);
bool lobby_handle_join_decision(ClientSession *host, bool accept, const char **err_msg);
void lobby_cancel_game(ClientSession *host);

/* Notifica eventi di gioco */
void game_check_and_start_combat(Game *g);

#endif /* LOBBY_H */
