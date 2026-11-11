#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "network.h"
#include "lobby.h"
#include "game.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile sig_atomic_t g_server_running = 1;
static int g_server_fd = -1;

static void sigint_handler(int sig) {
    (void)sig;
    g_server_running = 0;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

static void handle_client_disconnect(ClientSession *client) {
    if (!client) return;
    lobby_remove_client(client);
}

static void process_client_command(ClientSession *client, const char *line) {
    char cmd[64];
    cmd[0] = '\0';

    char *trimmed = trim_whitespace((char *)line);
    if (!trimmed || strlen(trimmed) == 0) return;

    if (sscanf(trimmed, "%63s", cmd) != 1) return;

    /* NICK <nome> */
    if (strcmp(cmd, "NICK") == 0) {
        char nick[MAX_NICK_LEN];
        if (sscanf(trimmed, "%*s %31s", nick) == 1) {
            safe_str_copy(client->nickname, nick, sizeof(client->nickname));
            client->state = STATE_LOBBY;
            client_send(client, "OK Nickname impostato: %s", client->nickname);
            log_info("Client (fd=%d) registrato con nickname: %s", client->socket_fd, client->nickname);
        } else {
            client_send(client, "ERR_INVALID_ARGS Uso: NICK <nickname>");
        }
        return;
    }

    /* Tutte le altre azioni richiedono che sia stato impostato un nickname */
    if (client->state == STATE_CONNECTED) {
        client_send(client, "ERR_NOT_REGISTERED Devi prima impostare un nickname con NICK <nome>");
        return;
    }

    /* LIST_GAMES */
    if (strcmp(cmd, "LIST_GAMES") == 0) {
        if (client->state != STATE_LOBBY) {
            client_send(client, "ERR_INVALID_STATE Puoi richiedere la lista partite solo dalla Lobby.");
            return;
        }
        lobby_send_games_list(client);
        return;
    }

    /* CREATE_GAME <nome> */
    if (strcmp(cmd, "CREATE_GAME") == 0) {
        if (client->state != STATE_LOBBY) {
            client_send(client, "ERR_INVALID_STATE Sei già in una partita o in attesa.");
            return;
        }
        char game_name[MAX_GAME_NAME_LEN];
        if (sscanf(trimmed, "%*s %31s", game_name) != 1) {
            snprintf(game_name, sizeof(game_name), "Partita_%.23s", client->nickname);
        }
        const char *err = NULL;
        int gid = lobby_create_game(client, game_name, &err);
        if (gid > 0) {
            client_send(client, "GAME_CREATED %d %s Partita creata con successo. In attesa di avversari...", gid, game_name);
        } else {
            client_send(client, "ERR_CREATE_GAME %s", err ? err : "Impossibile creare la partita.");
        }
        return;
    }

    /* JOIN_GAME <id> */
    if (strcmp(cmd, "JOIN_GAME") == 0) {
        if (client->state != STATE_LOBBY) {
            client_send(client, "ERR_INVALID_STATE Puoi unirti a una partita solo dalla Lobby.");
            return;
        }
        int target_id = -1;
        if (sscanf(trimmed, "%*s %d", &target_id) != 1) {
            client_send(client, "ERR_INVALID_ARGS Uso: JOIN_GAME <id_partita>");
            return;
        }
        const char *err = NULL;
        if (lobby_request_join(client, target_id, &err)) {
            client_send(client, "OK Richiesta inviata. In attesa che l'host accetti...");
        } else {
            client_send(client, "ERR_JOIN %s", err ? err : "Errore richiesta partecipazione.");
        }
        return;
    }

    /* ACCEPT_JOIN <1|0> */
    if (strcmp(cmd, "ACCEPT_JOIN") == 0) {
        if (client->state != STATE_CHALLENGE_PENDING) {
            client_send(client, "ERR_INVALID_STATE Nessuna richiesta in sospeso da accettare o rifiutare.");
            return;
        }
        int decision = -1;
        if (sscanf(trimmed, "%*s %d", &decision) != 1) {
            client_send(client, "ERR_INVALID_ARGS Uso: ACCEPT_JOIN <1 per accettare, 0 per rifiutare>");
            return;
        }
        const char *err = NULL;
        if (!lobby_handle_join_decision(client, (decision == 1), &err)) {
            client_send(client, "ERR_ACCEPT %s", err ? err : "Errore gestione risposta.");
        }
        return;
    }

    /* CANCEL_GAME */
    if (strcmp(cmd, "CANCEL_GAME") == 0) {
        if (client->state != STATE_WAITING_OPPONENT && client->state != STATE_CHALLENGE_PENDING) {
            client_send(client, "ERR_INVALID_STATE Non hai creato nessuna partita in attesa.");
            return;
        }
        lobby_cancel_game(client);
        client_send(client, "OK Partita annullata con successo. Sei tornato nella Lobby.");
        return;
    }

    /* PLACE_SHIP <ship_idx> <row> <col> <H|V> */
    if (strcmp(cmd, "PLACE_SHIP") == 0) {
        if (client->state != STATE_PLACING_SHIPS) {
            client_send(client, "ERR_INVALID_STATE Non sei nella fase di posizionamento delle navi.");
            return;
        }
        int ship_idx = -1, row = -1, col = -1;
        char dir_char = 0;
        if (sscanf(trimmed, "%*s %d %d %d %c", &ship_idx, &row, &col, &dir_char) != 4) {
            client_send(client, "ERR_INVALID_ARGS Uso: PLACE_SHIP <indice_nave> <riga> <colonna> <H|V|D|S|B|A>");
            return;
        }

        char dc = (char)toupper((unsigned char)dir_char);
        ShipDirection dir = DIR_HORIZONTAL;
        if (dc == 'S' || dc == 'L') {
            dir = DIR_LEFT;
        } else if (dc == 'A' || dc == 'U') {
            dir = DIR_UP;
        } else if (dc == 'V' || dc == 'B') {
            dir = DIR_DOWN;
        } else {
            dir = DIR_RIGHT; /* 'H', 'D', 'R' */
        }

        Game *g = lobby_get_game(client->active_game_id);
        if (!g) {
            client_send(client, "ERR_GAME_NOT_FOUND Partita non trovata.");
            return;
        }

        const char *err = NULL;
        if (game_place_ship(g, client, ship_idx, row, col, dir, &err)) {
            client_send(client, "SHIP_PLACED %d %s posizionata con successo.", ship_idx, SHIP_SPECS[ship_idx].name);
            Board *b = (client == g->host) ? &g->host_board : &g->guest_board;
            if (b->ships_placed == NUM_SHIPS) {
                client->state = STATE_PLACING_WAITING;
                client_send(client, "PLACEMENT_COMPLETE Tutte le 5 navi posizionate! In attesa dell'avversario...");
                game_check_and_start_combat(g);
            }
        } else {
            client_send(client, "ERR_PLACE %s", err ? err : "Posizionamento non valido.");
        }
        return;
    }

    /* AUTO_PLACE */
    if (strcmp(cmd, "AUTO_PLACE") == 0) {
        if (client->state != STATE_PLACING_SHIPS) {
            client_send(client, "ERR_INVALID_STATE Non sei nella fase di posizionamento delle navi.");
            return;
        }
        Game *g = lobby_get_game(client->active_game_id);
        if (!g) {
            client_send(client, "ERR_GAME_NOT_FOUND Partita non trovata.");
            return;
        }
        if (game_auto_place(g, client)) {
            client->state = STATE_PLACING_WAITING;
            client_send(client, "PLACEMENT_COMPLETE Flotta posizionata automaticamente! In attesa dell'avversario...");
            game_check_and_start_combat(g);
        } else {
            client_send(client, "ERR_PLACE Impossibile completare il posizionamento automatico.");
        }
        return;
    }

    /* FIRE <row> <col> */
    if (strcmp(cmd, "FIRE") == 0) {
        if (client->state == STATE_IN_GAME_WAIT_TURN) {
            client_send(client, "ERR_NOT_YOUR_TURN Non è il tuo turno! Attendi che l'avversario spari.");
            return;
        }
        if (client->state != STATE_IN_GAME_MY_TURN) {
            client_send(client, "ERR_INVALID_STATE Non puoi sparare in questo momento.");
            return;
        }

        int row = -1, col = -1;
        if (sscanf(trimmed, "%*s %d %d", &row, &col) != 2) {
            client_send(client, "ERR_INVALID_ARGS Uso: FIRE <riga 0-9> <colonna 0-9>");
            return;
        }

        Game *g = lobby_get_game(client->active_game_id);
        if (!g) {
            client_send(client, "ERR_GAME_NOT_FOUND Partita non trovata.");
            return;
        }

        ShotResult res = SHOT_INVALID;
        int hit_ship_idx = -1;
        bool is_game_over = false;
        const char *err = NULL;

        if (!game_handle_shot(g, client, row, col, &res, &hit_ship_idx, &is_game_over, &err)) {
            client_send(client, "ERR_FIRE %s", err ? err : "Colpo non valido.");
            return;
        }

        const char *res_str = (res == SHOT_MISS) ? "MISS" : ((res == SHOT_HIT) ? "HIT" : "SUNK");
        const char *ship_name = (hit_ship_idx >= 0 && hit_ship_idx < NUM_SHIPS) ? SHIP_SPECS[hit_ship_idx].name : "Nessuna";

        ClientSession *opponent = (client == g->host) ? g->guest : g->host;

        if (is_game_over) {
            /* Partita finita: vittoria per lo sparatore, sconfitta per l'avversario */
            client->state = STATE_GAME_OVER;
            client_send(client, "SHOT_RESULT YOU %d %d %s %s 0", row, col, res_str, ship_name);
            client_send(client, "GAME_OVER WIN Vittoria! Hai distrutto tutte le navi nemiche!");

            if (opponent && opponent->active) {
                opponent->state = STATE_GAME_OVER;
                client_send(opponent, "SHOT_RESULT OPPONENT %d %d %s %s 0", row, col, res_str, ship_name);
                client_send(opponent, "GAME_OVER LOSE Tutte le tue navi sono state affondate. Hai perso!");
            }
            log_info("Partita %d vinta da %s contro %s", g->id, client->nickname, opponent ? opponent->nickname : "?");
        } else {
            /* Turno completato: passaggio all'avversario */
            client->state = STATE_IN_GAME_WAIT_TURN;
            client_send(client, "SHOT_RESULT YOU %d %d %s %s 0", row, col, res_str, ship_name);
            client_send(client, "WAIT_TURN Turno passato all'avversario.");

            if (opponent && opponent->active) {
                opponent->state = STATE_IN_GAME_MY_TURN;
                client_send(opponent, "SHOT_RESULT OPPONENT %d %d %s %s 1", row, col, res_str, ship_name);
                client_send(opponent, "YOUR_TURN Tocca a te dichiarare le coordinate del colpo!");
            }
        }
        return;
    }

    /* REMATCH <1|0> */
    if (strcmp(cmd, "REMATCH") == 0) {
        if (client->state != STATE_GAME_OVER) {
            client_send(client, "ERR_INVALID_STATE Puoi richiedere una rivincita solo a fine partita.");
            return;
        }
        int choice = -1;
        if (sscanf(trimmed, "%*s %d", &choice) != 1) {
            client_send(client, "ERR_INVALID_ARGS Uso: REMATCH <1 per rivincita, 0 per uscire>");
            return;
        }

        Game *g = lobby_get_game(client->active_game_id);
        if (!g) {
            client->state = STATE_LOBBY;
            client->active_game_id = -1;
            client_send(client, "OK Tornato nella Lobby.");
            return;
        }

        pthread_mutex_lock(&g->lock);
        ClientSession *opponent = (client == g->host) ? g->guest : g->host;

        if (choice == 0) {
            /* Uno dei due rifiuta */
            pthread_mutex_unlock(&g->lock);
            if (opponent && opponent->active) {
                client_send(opponent, "REMATCH_RESULT DECLINED %s ha rifiutato la rivincita. Ritorno alla lobby.", client->nickname);
                opponent->state = STATE_LOBBY;
                opponent->active_game_id = -1;
            }
            client_send(client, "REMATCH_RESULT DECLINED Hai rifiutato la rivincita. Ritorno alla lobby.");
            client->state = STATE_LOBBY;
            client->active_game_id = -1;

            pthread_mutex_lock(&g->lock);
            g->in_use = false;
            pthread_mutex_unlock(&g->lock);
        } else {
            /* Vuole la rivincita */
            if (client == g->host) g->host_rematch = 1;
            else g->guest_rematch = 1;

            if (g->host_rematch == 1 && g->guest_rematch == 1) {
                /* Entrambi hanno accettato: resetta la partita e riavvia */
                board_reset(&g->host_board);
                board_reset(&g->guest_board);
                g->host_placement_done = false;
                g->guest_placement_done = false;
                g->current_turn = 1;
                g->game_over = false;
                g->winner = 0;
                g->host_rematch = -1;
                g->guest_rematch = -1;

                pthread_mutex_unlock(&g->lock);

                client->state = STATE_PLACING_SHIPS;
                if (opponent) opponent->state = STATE_PLACING_SHIPS;

                client_send(client, "REMATCH_RESULT ACCEPTED Nuova sfida avviata! Posiziona le tue navi.");
                if (opponent) client_send(opponent, "REMATCH_RESULT ACCEPTED Nuova sfida avviata! Posiziona le tue navi.");
                client_send(client, "START_PLACEMENT");
                if (opponent) client_send(opponent, "START_PLACEMENT");
            } else {
                pthread_mutex_unlock(&g->lock);
                client_send(client, "OK Richiesta rivincita inviata. In attesa di risposta dell'avversario...");
                if (opponent && opponent->active) {
                    client_send(opponent, "REMATCH_OFFERED %s desidera una rivincita! Vuoi accettare? (1=Si, 0=No)", client->nickname);
                }
            }
        }
        return;
    }

    /* OPPONENT_LEFT_CHOICE <1|2> (Opzionale da traccia) */
    if (strcmp(cmd, "OPPONENT_LEFT_CHOICE") == 0) {
        if (client->state != STATE_OPPONENT_LEFT) {
            client_send(client, "ERR_INVALID_STATE Il tuo avversario è ancora in partita.");
            return;
        }
        int choice = -1;
        if (sscanf(trimmed, "%*s %d", &choice) != 1) {
            client_send(client, "ERR_INVALID_ARGS Uso: OPPONENT_LEFT_CHOICE <1=Termina con vittoria, 2=Attendi nuovo sfidante>");
            return;
        }

        Game *g = lobby_get_game(client->active_game_id);
        if (choice == 1) {
            /* Termina con vittoria a tavolino */
            client_send(client, "GAME_OVER WIN Partita conclusa per forfait dell'avversario. Vittoria assegnata a te!");
            if (g) {
                pthread_mutex_lock(&g->lock);
                g->in_use = false;
                pthread_mutex_unlock(&g->lock);
            }
            client->state = STATE_LOBBY;
            client->active_game_id = -1;
        } else {
            /* Torna in attesa di un altro giocatore */
            if (g) {
                pthread_mutex_lock(&g->lock);
                g->host = client;
                g->guest = NULL;
                g->pending_challenger = NULL;
                board_reset(&g->host_board);
                board_reset(&g->guest_board);
                g->host_placement_done = false;
                g->guest_placement_done = false;
                g->game_over = false;
                pthread_mutex_unlock(&g->lock);
            }
            client->state = STATE_WAITING_OPPONENT;
            client_send(client, "OK Sei tornato in attesa di un nuovo giocatore nella lobby.");
        }
        return;
    }

    /* QUIT */
    if (strcmp(cmd, "QUIT") == 0) {
        client_send(client, "BYE Arrivederci!");
        handle_client_disconnect(client);
        return;
    }

    /* Comando sconosciuto */
    client_send(client, "ERR_UNKNOWN_CMD Comando non riconosciuto: %s", cmd);
}

static void *client_thread_worker(void *arg) {
    ClientSession *client = (ClientSession *)arg;
    pthread_detach(pthread_self());

    char line_buf[MAX_LINE_LEN];

    /* Messaggio di benvenuto */
    client_send(client, "WELCOME Benvenuto nel server di Battaglia Navale!");
    client_send(client, "INFO Per favore, imposta il tuo nickname digitando: NICK <tuo_nickname>");

    while (g_server_running && client->active) {
        int n = recv_line(client->socket_fd, line_buf, sizeof(line_buf));
        if (n <= 0) {
            /* Disconnessione o errore socket */
            break;
        }

        process_client_command(client, line_buf);
    }

    handle_client_disconnect(client);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        if (!parse_int_in_range(argv[1], 1024, 65535, &port)) {
            fprintf(stderr, "Uso: %s [porta (1024-65535, default: %d)]\n", argv[0], DEFAULT_PORT);
            return EXIT_FAILURE;
        }
    }

    /* Ignora SIGPIPE per evitare terminazioni su scritture verso socket chiuse */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Gestione SIGINT e SIGTERM per shutdown controllato */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    lobby_init();

    g_server_fd = setup_server_socket(port, 16);
    if (g_server_fd < 0) {
        log_error("Impossibile avviare il server sulla porta %d.", port);
        lobby_destroy();
        return EXIT_FAILURE;
    }

    log_info("=== Server Battaglia Navale avviato sulla porta %d ===", port);
    log_info("In attesa di connessioni client... (Premi Ctrl+C per terminare)");

    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (!g_server_running) break;
                continue;
            }
            if (!g_server_running) break;
            log_warn("accept() fallita: %s", strerror(errno));
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        log_info("Nuova connessione accettata da %s:%d (fd=%d)", ip_str, ntohs(client_addr.sin_port), client_fd);

        ClientSession *session = lobby_add_client(client_fd);
        if (!session) {
            log_warn("Raggiunto il limite massimo di client connessi (%d). Connessione rifiutata.", MAX_CLIENTS);
            send_line(client_fd, "ERR_SERVER_FULL Server pieno. Riprova più tardi.");
            close(client_fd);
            continue;
        }

        if (pthread_create(&session->thread_id, NULL, client_thread_worker, session) != 0) {
            log_error("pthread_create fallita: %s", strerror(errno));
            lobby_remove_client(session);
        }
    }

    log_info("Chiusura controllata del server in corso...");
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    lobby_destroy();
    log_info("Server terminato correttamente.");

    return EXIT_SUCCESS;
}
