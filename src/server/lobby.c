#include "lobby.h"
#include "network.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

static ClientSession g_clients[MAX_CLIENTS];
static Game g_games[MAX_GAMES];
static pthread_mutex_t g_lobby_mutex = PTHREAD_MUTEX_INITIALIZER;

void lobby_init(void) {
    pthread_mutex_lock(&g_lobby_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&g_clients[i], 0, sizeof(ClientSession));
        g_clients[i].socket_fd = -1;
        g_clients[i].active_game_id = -1;
        g_clients[i].state = STATE_CONNECTED;
        g_clients[i].active = false;
        pthread_mutex_init(&g_clients[i].send_lock, NULL);
    }

    for (int i = 0; i < MAX_GAMES; i++) {
        memset(&g_games[i], 0, sizeof(Game));
        g_games[i].id = i + 1;
        g_games[i].in_use = false;
        pthread_mutex_init(&g_games[i].lock, NULL);
    }
    pthread_mutex_unlock(&g_lobby_mutex);
}

void lobby_destroy(void) {
    pthread_mutex_lock(&g_lobby_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].socket_fd >= 0) {
            close(g_clients[i].socket_fd);
            g_clients[i].socket_fd = -1;
        }
        pthread_mutex_destroy(&g_clients[i].send_lock);
    }

    for (int i = 0; i < MAX_GAMES; i++) {
        pthread_mutex_destroy(&g_games[i].lock);
    }
    pthread_mutex_unlock(&g_lobby_mutex);
    pthread_mutex_destroy(&g_lobby_mutex);
}

ClientSession *lobby_add_client(int socket_fd) {
    pthread_mutex_lock(&g_lobby_mutex);
    ClientSession *session = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].active) {
            session = &g_clients[i];
            session->socket_fd = socket_fd;
            session->nickname[0] = '\0';
            session->state = STATE_CONNECTED;
            session->active_game_id = -1;
            session->active = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_lobby_mutex);
    return session;
}

int client_send(ClientSession *client, const char *fmt, ...) {
    if (!client || !client->active || client->socket_fd < 0) return -1;

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

    pthread_mutex_lock(&client->send_lock);
    ssize_t sent = send_all(client->socket_fd, buffer, (size_t)(written + 1));
    pthread_mutex_unlock(&client->send_lock);

    return (sent == written + 1) ? 0 : -1;
}

int lobby_create_game(ClientSession *host, const char *game_name, const char **err_msg) {
    if (!host || !game_name) {
        if (err_msg) *err_msg = "Parametri non validi.";
        return -1;
    }

    pthread_mutex_lock(&g_lobby_mutex);

    int free_slot = -1;
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!g_games[i].in_use) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Numero massimo di partite contemporanee raggiunto sul server.";
        return -1;
    }

    Game *g = &g_games[free_slot];
    game_init(g, free_slot + 1, game_name, host);

    host->state = STATE_WAITING_OPPONENT;
    host->active_game_id = g->id;

    int assigned_id = g->id;
    pthread_mutex_unlock(&g_lobby_mutex);

    log_info("Nuova partita creata: ID %d, Nome '%s', Host '%s'", assigned_id, game_name, host->nickname);
    return assigned_id;
}

Game *lobby_get_game(int game_id) {
    if (game_id < 1 || game_id > MAX_GAMES) return NULL;
    pthread_mutex_lock(&g_lobby_mutex);
    Game *g = &g_games[game_id - 1];
    if (!g->in_use) {
        pthread_mutex_unlock(&g_lobby_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&g_lobby_mutex);
    return g;
}

void lobby_send_games_list(ClientSession *client) {
    if (!client) return;

    pthread_mutex_lock(&g_lobby_mutex);
    int count = 0;
    for (int i = 0; i < MAX_GAMES; i++) {
        if (g_games[i].in_use && g_games[i].guest == NULL && g_games[i].pending_challenger == NULL) {
            count++;
        }
    }

    client_send(client, "GAMES_LIST_START %d", count);
    for (int i = 0; i < MAX_GAMES; i++) {
        if (g_games[i].in_use && g_games[i].guest == NULL && g_games[i].pending_challenger == NULL) {
            client_send(client, "GAME_ITEM %d %s %s", g_games[i].id, g_games[i].name,
                        g_games[i].host ? g_games[i].host->nickname : "Unknown");
        }
    }
    client_send(client, "GAMES_LIST_END");
    pthread_mutex_unlock(&g_lobby_mutex);
}

bool lobby_request_join(ClientSession *guest, int game_id, const char **err_msg) {
    if (!guest || game_id < 1 || game_id > MAX_GAMES) {
        if (err_msg) *err_msg = "ID partita non valido.";
        return false;
    }

    pthread_mutex_lock(&g_lobby_mutex);
    Game *g = &g_games[game_id - 1];

    if (!g->in_use) {
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Partita non trovata o non più disponibile.";
        return false;
    }

    pthread_mutex_lock(&g->lock);

    if (g->host == guest) {
        pthread_mutex_unlock(&g->lock);
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Non puoi unirti alla partita che hai creato tu stesso!";
        return false;
    }

    if (g->guest != NULL) {
        pthread_mutex_unlock(&g->lock);
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Partita già piena o in corso.";
        return false;
    }

    if (g->pending_challenger != NULL) {
        pthread_mutex_unlock(&g->lock);
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Un altro giocatore ha già fatto richiesta per questa partita. Riprova tra poco.";
        return false;
    }

    g->pending_challenger = guest;
    guest->state = STATE_WAITING_ACCEPT;
    guest->active_game_id = g->id;

    if (g->host) {
        g->host->state = STATE_CHALLENGE_PENDING;
        client_send(g->host, "CHALLENGE_REQUEST %s", guest->nickname);
    }

    pthread_mutex_unlock(&g->lock);
    pthread_mutex_unlock(&g_lobby_mutex);

    return true;
}

bool lobby_handle_join_decision(ClientSession *host, bool accept, const char **err_msg) {
    if (!host || host->active_game_id < 1 || host->active_game_id > MAX_GAMES) {
        if (err_msg) *err_msg = "Partita non valida.";
        return false;
    }

    pthread_mutex_lock(&g_lobby_mutex);
    Game *g = &g_games[host->active_game_id - 1];

    pthread_mutex_lock(&g->lock);

    if (g->host != host || g->pending_challenger == NULL) {
        pthread_mutex_unlock(&g->lock);
        pthread_mutex_unlock(&g_lobby_mutex);
        if (err_msg) *err_msg = "Nessuna richiesta di partecipazione in sospeso.";
        return false;
    }

    ClientSession *guest = g->pending_challenger;

    if (!accept) {
        g->pending_challenger = NULL;
        host->state = STATE_WAITING_OPPONENT;

        if (guest && guest->active) {
            guest->state = STATE_LOBBY;
            guest->active_game_id = -1;
            client_send(guest, "JOIN_REJECTED La tua richiesta è stata rifiutata dal creatore della partita.");
        }
        client_send(host, "OK Richiesta rifiutata. La tua stanza è di nuovo aperta ad altri sfidanti.");
    } else {
        g->guest = guest;
        g->pending_challenger = NULL;
        host->state = STATE_PLACING_SHIPS;
        guest->state = STATE_PLACING_SHIPS;

        client_send(host, "JOIN_ACCEPTED %d %s %s", g->id, guest->nickname, g->name);
        client_send(guest, "JOIN_ACCEPTED %d %s %s", g->id, host->nickname, g->name);

        client_send(host, "START_PLACEMENT Posiziona le tue 5 navi sulla griglia di battaglia.");
        client_send(guest, "START_PLACEMENT Posiziona le tue 5 navi sulla griglia di battaglia.");
    }

    pthread_mutex_unlock(&g->lock);
    pthread_mutex_unlock(&g_lobby_mutex);
    return true;
}

void game_check_and_start_combat(Game *g) {
    if (!g) return;

    pthread_mutex_lock(&g->lock);
    if (g->host_placement_done && g->guest_placement_done && !g->game_over && g->host && g->guest) {
        g->current_turn = 1; /* Host inizia per primo */
        g->host->state = STATE_IN_GAME_MY_TURN;
        g->guest->state = STATE_IN_GAME_WAIT_TURN;

        client_send(g->host, "COMBAT_START 1 %s", g->guest->nickname);
        client_send(g->guest, "COMBAT_START 0 %s", g->host->nickname);

        log_info("Combattimento iniziato nella partita %d (%s vs %s)", g->id, g->host->nickname, g->guest->nickname);
    }
    pthread_mutex_unlock(&g->lock);
}

void lobby_cancel_game(ClientSession *host) {
    if (!host || host->active_game_id < 1 || host->active_game_id > MAX_GAMES) return;

    pthread_mutex_lock(&g_lobby_mutex);
    Game *g = &g_games[host->active_game_id - 1];
    pthread_mutex_lock(&g->lock);

    if (g->in_use && g->host == host) {
        if (g->pending_challenger && g->pending_challenger->active) {
            g->pending_challenger->state = STATE_LOBBY;
            g->pending_challenger->active_game_id = -1;
            client_send(g->pending_challenger, "ERR_GAME_CANCELLED La partita è stata annullata dal creatore.");
        }
        if (g->guest && g->guest->active) {
            g->guest->state = STATE_LOBBY;
            g->guest->active_game_id = -1;
            client_send(g->guest, "ERR_GAME_CANCELLED La partita è stata annullata dall'host.");
        }
        g->in_use = false;
        g->host = NULL;
        g->guest = NULL;
        g->pending_challenger = NULL;
    }

    host->active_game_id = -1;
    host->state = STATE_LOBBY;

    pthread_mutex_unlock(&g->lock);
    pthread_mutex_unlock(&g_lobby_mutex);
}

void lobby_remove_client(ClientSession *client) {
    if (!client) return;

    pthread_mutex_lock(&g_lobby_mutex);
    if (!client->active) {
        pthread_mutex_unlock(&g_lobby_mutex);
        return;
    }

    log_info("Disconnessione client '%s' (socket fd=%d)", client->nickname[0] ? client->nickname : "Anomimo", client->socket_fd);

    if (client->active_game_id >= 1 && client->active_game_id <= MAX_GAMES) {
        Game *g = &g_games[client->active_game_id - 1];
        pthread_mutex_lock(&g->lock);

        if (g->in_use) {
            if (g->pending_challenger == client) {
                g->pending_challenger = NULL;
                if (g->host && g->host->active) {
                    g->host->state = STATE_WAITING_OPPONENT;
                    client_send(g->host, "INFO Lo sfidante si è disconnesso.");
                }
            } else if (g->host == client && g->guest == NULL) {
                /* Host abbandona mentre era solo in attesa */
                if (g->pending_challenger && g->pending_challenger->active) {
                    g->pending_challenger->state = STATE_LOBBY;
                    g->pending_challenger->active_game_id = -1;
                    client_send(g->pending_challenger, "ERR_GAME_CANCELLED L'host si è disconnesso prima di accettare la sfida.");
                    g->pending_challenger = NULL;
                }
                g->in_use = false;
                g->host = NULL;
            } else {
                /* Partita in corso o sfidante già accettato */
                ClientSession *other = (client == g->host) ? g->guest : g->host;
                if (other && other->active) {
                    other->state = STATE_OPPONENT_LEFT;
                    client_send(other, "OPPONENT_DISCONNECTED %s", client->nickname);
                }
                /* Se client era host, assegniamo ad interim NULL */
                if (client == g->host) g->host = NULL;
                if (client == g->guest) g->guest = NULL;
                if (!g->host && !g->guest) {
                    g->in_use = false;
                }
            }
        }

        pthread_mutex_unlock(&g->lock);
    }

    client->active = false;
    client->state = STATE_CONNECTED;
    client->active_game_id = -1;
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }

    pthread_mutex_unlock(&g_lobby_mutex);
}
