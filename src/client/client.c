#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "network.h"
#include "board.h"
#include "ui.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

static volatile sig_atomic_t g_client_running = 1;
static int g_sock_fd = -1;

static void sigint_handler(int sig) {
    (void)sig;
    g_client_running = 0;
    if (g_sock_fd >= 0) {
        send_line(g_sock_fd, "QUIT");
        close(g_sock_fd);
        g_sock_fd = -1;
    }
    printf("\n\033[1;33m[!] Chiusura client in corso...\033[0m\n");
    exit(0);
}

/* Esiti della fase di combattimento */
enum CombatOutcome {
    OUTCOME_ERROR = 0,
    OUTCOME_VICTORY = 1,
    OUTCOME_DEFEAT = 2,
    OUTCOME_FORFEIT_WIN = 3,
    OUTCOME_WAIT_NEW_CHALLENGER = 4
};

/* Prototipi funzioni mutualmente richiamate */
static void host_wait_for_challenger(int sock);
static void run_game_session(int sock, const char *opponent_nick, bool start_my_turn);
static bool ask_rematch(int sock, const char *opponent_nick);

/* Attende e legge una linea dal server, gestendo eventuali disconnessioni */
static int read_server_line(int sock, char *buf, size_t size) {
    int n = recv_line(sock, buf, size);
    if (n <= 0) {
        printf("\n\033[1;31m[!] Connessione con il server interrotta.\033[0m\n");
        return -1;
    }
    return n;
}

/* Gestione piazzamento navi */
static int do_ship_placement(int sock, Board *my_board) {
    board_init(my_board);

    ui_print_header("FASE 1: POSIZIONAMENTO DELLA FLOTTA");
    printf("Devi posizionare 5 navi sulla tua griglia 10x10:\n");
    for (int i = 0; i < NUM_SHIPS; i++) {
        printf("  %d) %-18s [%d caselle]\n", i + 1, SHIP_SPECS[i].name, SHIP_SPECS[i].size);
    }

    printf("\nScegli la modalità di posizionamento:\n");
    printf("  1) Posizionamento MANUALE (coordinate e orientamento per ogni nave)\n");
    printf("  2) Posizionamento AUTOMATICO (generazione casuale rapida e valida)\n");

    int mode = 1;
    if (get_valid_int("Scelta (1 o 2): ", 1, 2, &mode) < 0) return -1;

    if (mode == 2) {
        /* Posizionamento automatico calcolato dal client e inviato al server */
        board_auto_place_all(my_board);
        printf("\nPosizionamento automatico generato con successo!\n");
        ui_print_single_board(my_board, "LA TUA FLOTTA GENERATA");

        for (int i = 0; i < NUM_SHIPS; i++) {
            char dir_c = (my_board->ships[i].dir == DIR_VERTICAL) ? 'V' : 'H';
            send_line(sock, "PLACE_SHIP %d %d %d %c", i, my_board->ships[i].row, my_board->ships[i].col, dir_c);
            char resp[BUFFER_SIZE];
            if (read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
        }

        /* Attende conferma di completamento placement dal server */
        char resp[BUFFER_SIZE];
        if (read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
        return 0;
    }

    /* Posizionamento manuale */
    for (int i = 0; i < NUM_SHIPS; i++) {
        bool placed = false;
        while (!placed && g_client_running) {
            ui_print_single_board(my_board, "POSIZIONAMENTO IN CORSO");
            printf("\n\033[1;36m>> Posiziona nave %d di %d: %s (%d caselle) <<\033[0m\n",
                   i + 1, NUM_SHIPS, SHIP_SPECS[i].name, SHIP_SPECS[i].size);

            int r = -1, c = -1;
            if (get_valid_coords("Coordinata di partenza (es. B4, A1, J10): ", &r, &c) < 0) return -1;

            char dir_buf[32];
            ShipDirection dir = DIR_HORIZONTAL;
            char dir_c = 'H';
            bool orientation_chosen = false;

            while (!orientation_chosen && g_client_running) {
                printf("Orientamento [H = Orizzontale, V = Verticale]: ");
                fflush(stdout);
                if (safe_read_line(dir_buf, sizeof(dir_buf)) < 0) return -1;
                char *td = trim_whitespace(dir_buf);
                if (!td || strlen(td) == 0) continue;
                char choice = (char)toupper((unsigned char)td[0]);

                if (choice == 'H') {
                    bool can_right = board_can_place_ship(my_board, i, r, c, DIR_RIGHT, NULL);
                    bool can_left  = board_can_place_ship(my_board, i, r, c, DIR_LEFT, NULL);

                    if (can_right && can_left) {
                        /* Vi è la possibilità di scegliere entrambi: chiediamo all'utente */
                        while (1) {
                            printf("  Direzione orizzontale [D = Verso Destra, S = Verso Sinistra]: ");
                            fflush(stdout);
                            if (safe_read_line(dir_buf, sizeof(dir_buf)) < 0) return -1;
                            char *sub = trim_whitespace(dir_buf);
                            if (sub && (toupper((unsigned char)sub[0]) == 'D' || toupper((unsigned char)sub[0]) == 'R')) {
                                dir = DIR_RIGHT;
                                dir_c = 'D';
                                break;
                            } else if (sub && (toupper((unsigned char)sub[0]) == 'S' || toupper((unsigned char)sub[0]) == 'L')) {
                                dir = DIR_LEFT;
                                dir_c = 'S';
                                break;
                            }
                            printf("\033[1;31m  [!] Inserisci D per Destra o S per Sinistra.\033[0m\n");
                        }
                        orientation_chosen = true;
                    } else if (can_right) {
                        printf("\033[1;36m  [i] Verso sinistra non c'è spazio: posizionamento automatico verso Destra.\033[0m\n");
                        dir = DIR_RIGHT;
                        dir_c = 'D';
                        orientation_chosen = true;
                    } else if (can_left) {
                        printf("\033[1;36m  [i] Verso destra non c'è spazio: posizionamento automatico verso Sinistra.\033[0m\n");
                        dir = DIR_LEFT;
                        dir_c = 'S';
                        orientation_chosen = true;
                    } else {
                        printf("\033[1;31m[!] Nessuna direzione orizzontale valida (Destra o Sinistra) da questa coordinata.\033[0m\n");
                        break;
                    }
                } else if (choice == 'V') {
                    bool can_down = board_can_place_ship(my_board, i, r, c, DIR_DOWN, NULL);
                    bool can_up   = board_can_place_ship(my_board, i, r, c, DIR_UP, NULL);

                    if (can_down && can_up) {
                        /* Vi è la possibilità di scegliere entrambi: chiediamo all'utente */
                        while (1) {
                            printf("  Direzione verticale [B = Verso il Basso, A = Verso l'Alto]: ");
                            fflush(stdout);
                            if (safe_read_line(dir_buf, sizeof(dir_buf)) < 0) return -1;
                            char *sub = trim_whitespace(dir_buf);
                            if (sub && (toupper((unsigned char)sub[0]) == 'B' || toupper((unsigned char)sub[0]) == 'V')) {
                                dir = DIR_DOWN;
                                dir_c = 'B';
                                break;
                            } else if (sub && (toupper((unsigned char)sub[0]) == 'A' || toupper((unsigned char)sub[0]) == 'U')) {
                                dir = DIR_UP;
                                dir_c = 'A';
                                break;
                            }
                            printf("\033[1;31m  [!] Inserisci B per Basso o A per Alto.\033[0m\n");
                        }
                        orientation_chosen = true;
                    } else if (can_down) {
                        printf("\033[1;36m  [i] Verso l'alto non c'è spazio: posizionamento automatico verso il Basso.\033[0m\n");
                        dir = DIR_DOWN;
                        dir_c = 'B';
                        orientation_chosen = true;
                    } else if (can_up) {
                        printf("\033[1;36m  [i] Verso il basso non c'è spazio: posizionamento automatico verso l'Alto.\033[0m\n");
                        dir = DIR_UP;
                        dir_c = 'A';
                        orientation_chosen = true;
                    } else {
                        printf("\033[1;31m[!] Nessuna direzione verticale valida (In alto o In basso) da questa coordinata.\033[0m\n");
                        break;
                    }
                } else if (choice == 'D') {
                    dir = DIR_RIGHT; dir_c = 'D'; orientation_chosen = true;
                } else if (choice == 'S') {
                    dir = DIR_LEFT; dir_c = 'S'; orientation_chosen = true;
                } else if (choice == 'B') {
                    dir = DIR_DOWN; dir_c = 'B'; orientation_chosen = true;
                } else if (choice == 'A') {
                    dir = DIR_UP; dir_c = 'A'; orientation_chosen = true;
                } else {
                    printf("\033[1;31m[!] Scelta non valida. Inserisci H per orizzontale o V per verticale.\033[0m\n");
                }
            }

            if (!orientation_chosen) {
                continue;
            }

            const char *err = NULL;
            if (!board_can_place_ship(my_board, i, r, c, dir, &err)) {
                printf("\n\033[1;31m[!] Posizionamento non valido: %s\033[0m\n", err ? err : "errore sconosciuto");
                continue;
            }

            send_line(sock, "PLACE_SHIP %d %d %d %c", i, r, c, dir_c);

            char resp[BUFFER_SIZE];
            if (read_server_line(sock, resp, sizeof(resp)) < 0) return -1;

            if (strncmp(resp, "SHIP_PLACED", 11) == 0) {
                board_place_ship(my_board, i, r, c, dir);
                placed = true;
                printf("\033[1;32m[OK] %s posizionata con successo!\033[0m\n", SHIP_SPECS[i].name);
            } else {
                printf("\033[1;31m[!] Il server ha rifiutato la mossa: %s\033[0m\n", resp);
            }
        }
    }

    char resp[BUFFER_SIZE];
    if (read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
    ui_print_single_board(my_board, "FLOTTA PRONTA");
    return 0;
}

/* Gestione opzione disconnessione avversario (Requisito opzionale pag. 3) */
static int handle_opponent_disconnected(int sock) {
    ui_print_header("AVVISO DISCONNESSIONE AVVERSARIO");
    printf("\033[1;33mIl tuo avversario si è disconnesso dalla partita!\033[0m\n");
    printf("Come desideri procedere?\n");
    printf("  1) Termina la partita e ottieni la vittoria a tavolino\n");
    printf("  2) Rimani nella stanza e attendi che un altro giocatore si unisca\n");

    int choice = 1;
    if (get_valid_int("Scelta (1 o 2): ", 1, 2, &choice) < 0) return -1;

    send_line(sock, "OPPONENT_LEFT_CHOICE %d", choice);
    char resp[BUFFER_SIZE];
    if (read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
    printf("\n%s\n", resp);
    return choice;
}

/* Ciclo di combattimento a turni */
static int run_combat_loop(int sock, Board *my_board, const char *opponent_nick, bool start_my_turn) {
    CellState opp_radar[BOARD_SIZE][BOARD_SIZE];
    memset(opp_radar, 0, sizeof(opp_radar));

    bool my_turn = start_my_turn;
    char line[BUFFER_SIZE];

    ui_clear_screen();
    ui_print_header("FASE 2: COMBATTIMENTO NAVALE");
    printf("Partita avviata contro: \033[1;35m%s\033[0m\n", opponent_nick);

    while (g_client_running) {
        ui_print_boards(my_board, opp_radar);

        if (my_turn) {
            printf("\n\033[1;32m>>> È IL TUO TURNO! <<<\033[0m\n");
            int r = -1, c = -1;
            while (1) {
                if (get_valid_coords("Dichiara coordinate bersaglio di fuoco (es. B4): ", &r, &c) < 0) {
                    return -1;
                }
                if (opp_radar[r][c] != CELL_EMPTY) {
                    printf("\033[1;33m[!] Hai già sparato in %c%d! Scegli un'altra cella.\033[0m\n", 'A' + r, c + 1);
                    continue;
                }
                break;
            }

            send_line(sock, "FIRE %d %d", r, c);

            /* Leggi esito del colpo */
            if (read_server_line(sock, line, sizeof(line)) < 0) return -1;

            char shooter[32], outcome[32], ship_name[32];
            int shot_r = -1, shot_c = -1, next_turn = 0;
            int sunk_r = -1, sunk_c = -1, sunk_len = 0, sunk_dir = 0;
            int parsed = sscanf(line, "SHOT_RESULT %31s %d %d %31s %31s %d %d %d %d %d",
                                shooter, &shot_r, &shot_c, outcome, ship_name, &next_turn,
                                &sunk_r, &sunk_c, &sunk_len, &sunk_dir);
            if (parsed >= 4) {
                if (strcmp(outcome, "MISS") == 0) {
                    opp_radar[shot_r][shot_c] = CELL_MISS;
                    printf("\n\033[0;37m>>> ACQUA! Colpo a vuoto in %c%d <<<\033[0m\n", 'A' + shot_r, shot_c + 1);
                } else if (strcmp(outcome, "HIT") == 0) {
                    opp_radar[shot_r][shot_c] = CELL_HIT;
                    printf("\n\033[1;31m>>> COLPITO! Nave nemica centrata in %c%d! <<<\033[0m\n", 'A' + shot_r, shot_c + 1);
                } else if (strcmp(outcome, "SUNK") == 0) {
                    if (parsed >= 10 && sunk_len > 0) {
                        for (int i = 0; i < sunk_len; i++) {
                            int r = (sunk_dir == DIR_VERTICAL) ? (sunk_r + i) : sunk_r;
                            int c = (sunk_dir == DIR_HORIZONTAL) ? (sunk_c + i) : sunk_c;
                            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                                opp_radar[r][c] = CELL_SUNK;
                            }
                        }
                    } else {
                        opp_radar[shot_r][shot_c] = CELL_SUNK;
                    }
                    printf("\n\033[1;41;37m>>> COLPITO E AFFONDATO! Hai affondato: %s! <<<\033[0m\n", ship_name);
                }
            }

            /* Controlla prossimo messaggio (GAME_OVER, OPPONENT_DISCONNECTED o WAIT_TURN) */
            if (read_server_line(sock, line, sizeof(line)) < 0) return -1;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) {
                    printf("\n\033[1;32mPartita conclusa per abbandono dell'avversario. Vittoria assegnata a te!\033[0m\n");
                    printf("\033[1;33m[i] Nessuna rivincita possibile in quanto l'avversario si è disconnesso.\033[0m\n");
                    printf("Premi Invio per tornare alla lobby principale...");
                    char dummy[16];
                    safe_read_line(dummy, sizeof(dummy));
                    return OUTCOME_FORFEIT_WIN; /* Vittoria a tavolino: no rematch */
                } else if (res == 2) {
                    return OUTCOME_WAIT_NEW_CHALLENGER; /* Attesa nuovo giocatore */
                }
                return OUTCOME_ERROR;
            }

            if (strncmp(line, "GAME_OVER", 9) == 0) {
                ui_print_boards(my_board, opp_radar);
                printf("\n\033[1;32m");
                printf("************************************************************\n");
                printf("               VITTORIA! HAI VINTO LA PARTITA!              \n");
                printf("************************************************************\n");
                printf("\033[0m\n");
                return OUTCOME_VICTORY; /* Vittoria normale sul campo */
            } else if (strncmp(line, "WAIT_TURN", 9) == 0) {
                my_turn = false;
            }
        } else {
            printf("\n\033[1;33m--- In attesa del colpo di %s... ---\033[0m\n", opponent_nick);

            if (read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) {
                    printf("\n\033[1;32mPartita conclusa per abbandono dell'avversario. Vittoria assegnata a te!\033[0m\n");
                    printf("\033[1;33m[i] Nessuna rivincita possibile in quanto l'avversario si è disconnesso.\033[0m\n");
                    printf("Premi Invio per tornare alla lobby principale...");
                    char dummy[16];
                    safe_read_line(dummy, sizeof(dummy));
                    return OUTCOME_FORFEIT_WIN; /* Vittoria a tavolino: no rematch */
                } else if (res == 2) {
                    return OUTCOME_WAIT_NEW_CHALLENGER; /* Attesa nuovo giocatore */
                }
                return OUTCOME_ERROR;
            }

            char shooter[32], outcome[32], ship_name[32];
            int shot_r = -1, shot_c = -1, next_turn = 0;
            int sunk_r = -1, sunk_c = -1, sunk_len = 0, sunk_dir = 0;
            int parsed = sscanf(line, "SHOT_RESULT %31s %d %d %31s %31s %d %d %d %d %d",
                                shooter, &shot_r, &shot_c, outcome, ship_name, &next_turn,
                                &sunk_r, &sunk_c, &sunk_len, &sunk_dir);
            if (parsed >= 4) {
                if (strcmp(outcome, "MISS") == 0) {
                    if (my_board->grid[shot_r][shot_c] == CELL_EMPTY) {
                        my_board->grid[shot_r][shot_c] = CELL_MISS;
                    }
                    printf("\n\033[0;37m>>> %s ha sparato in %c%d: ACQUA! <<<\033[0m\n", opponent_nick, 'A' + shot_r, shot_c + 1);
                } else if (strcmp(outcome, "HIT") == 0) {
                    my_board->grid[shot_r][shot_c] = CELL_HIT;
                    printf("\n\033[1;31m>>> ALLARME! %s ha COLPITO una tua nave in %c%d! <<<\033[0m\n", opponent_nick, 'A' + shot_r, shot_c + 1);
                } else if (strcmp(outcome, "SUNK") == 0) {
                    if (parsed >= 10 && sunk_len > 0) {
                        for (int i = 0; i < sunk_len; i++) {
                            int r = (sunk_dir == DIR_VERTICAL) ? (sunk_r + i) : sunk_r;
                            int c = (sunk_dir == DIR_HORIZONTAL) ? (sunk_c + i) : sunk_c;
                            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                                my_board->grid[r][c] = CELL_SUNK;
                            }
                        }
                    } else {
                        int ship_idx = my_board->ship_map[shot_r][shot_c];
                        if (ship_idx >= 0 && ship_idx < NUM_SHIPS) {
                            int sr = my_board->ships[ship_idx].row;
                            int sc = my_board->ships[ship_idx].col;
                            int slen = my_board->ships[ship_idx].size;
                            ShipDirection sdir = my_board->ships[ship_idx].dir;
                            for (int i = 0; i < slen; i++) {
                                int r = (sdir == DIR_VERTICAL) ? (sr + i) : sr;
                                int c = (sdir == DIR_HORIZONTAL) ? (sc + i) : sc;
                                if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                                    my_board->grid[r][c] = CELL_SUNK;
                                }
                            }
                        } else {
                            my_board->grid[shot_r][shot_c] = CELL_SUNK;
                        }
                    }
                    printf("\n\033[1;41;37m>>> AFFONDATA! La tua nave '%s' è stata affondata da %s! <<<\033[0m\n", ship_name, opponent_nick);
                }
            }

            /* Controlla prossimo messaggio (GAME_OVER o YOUR_TURN) */
            if (read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "GAME_OVER", 9) == 0) {
                ui_print_boards(my_board, opp_radar);
                printf("\n\033[1;31m");
                printf("************************************************************\n");
                printf("           SCONFITTA! TUTTE LE TUE NAVI SONO AFFONDATE      \n");
                printf("************************************************************\n");
                printf("\033[0m\n");
                return OUTCOME_DEFEAT; /* Sconfitta */
            } else if (strncmp(line, "YOUR_TURN", 9) == 0) {
                my_turn = true;
            }
        }
    }

    return OUTCOME_ERROR;
}

/* Gestione richiesta di rivincita a fine partita con multiplazione I/O tramite select() */
static bool ask_rematch(int sock, const char *opponent_nick) {
    printf("\n\033[1;36m============================================================\033[0m\n");
    printf("Vuoi disputare una NUOVA SFIDA (rivincita) con \033[1;35m%s\033[0m?\n",
           opponent_nick ? opponent_nick : "l'avversario");
    printf("  1) Sì, richiedi rivincita!\n");
    printf("  2) No, torna alla lobby principale\n");
    printf("Scelta (1 o 2): ");
    fflush(stdout);

    fd_set readfds;
    char line[BUFFER_SIZE];
    bool rematch_requested = false;

    while (g_client_running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* 1) Notifiche dal server via socket */
        if (FD_ISSET(sock, &readfds)) {
            if (read_server_line(sock, line, sizeof(line)) < 0) {
                return false;
            }

            if (strncmp(line, "REMATCH_OFFERED", 15) == 0) {
                printf("\n\n\033[1;32m>>> [SFIDA RICEVUTA] %s desidera una rivincita! <<<\033[0m\n",
                       opponent_nick ? opponent_nick : "L'avversario");
                printf("Vuoi accettare la sfida? (1 = Accetta, 2 = Rifiuta ed esci): ");
                fflush(stdout);
            } else if (strncmp(line, "REMATCH_RESULT ACCEPTED", 23) == 0) {
                printf("\n\033[1;32m[OK] Entrambi avete accettato la rivincita! Si ricomincia!\033[0m\n");
                /* Leggi l'eventuale START_PLACEMENT che segue */
                read_server_line(sock, line, sizeof(line));
                return true;
            } else if (strncmp(line, "REMATCH_RESULT DECLINED", 23) == 0) {
                printf("\n\033[1;33m[!] %s ha rifiutato la rivincita / abbandonato la stanza. Ritorno alla lobby.\033[0m\n",
                       opponent_nick ? opponent_nick : "L'avversario");
                return false;
            } else if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                printf("\n\033[1;31m[!] L'avversario si è disconnesso dal server. Ritorno alla lobby.\033[0m\n");
                return false;
            } else if (strncmp(line, "OK", 2) == 0) {
                /* Acknowledge dal server, continuiamo ad attendere l'avversario */
                continue;
            }
        }

        /* 2) Input da terminale (attivo solo se l'utente non ha ancora inviato la sua scelta) */
        if (!rematch_requested && FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[64];
            if (safe_read_line(input, sizeof(input)) < 0) return false;
            char *trimmed = trim_whitespace(input);
            if (!trimmed || strlen(trimmed) == 0) {
                printf("Scelta (1 o 2): ");
                fflush(stdout);
                continue;
            }

            if (strcmp(trimmed, "1") == 0) {
                send_line(sock, "REMATCH 1");
                rematch_requested = true;
                printf("\n\033[1;36m[i] Richiesta di rivincita inviata! In attesa della risposta di %s...\033[0m\n",
                       opponent_nick ? opponent_nick : "avversario");
                fflush(stdout);
            } else if (strcmp(trimmed, "2") == 0) {
                send_line(sock, "REMATCH 0");
                printf("\nRivincita rifiutata. Ritorno alla lobby.\n");
                char resp[BUFFER_SIZE];
                read_server_line(sock, resp, sizeof(resp));
                return false;
            } else {
                printf("\033[1;31m[!] Scelta non valida. Inserisci 1 (Rivincita) o 2 (Lobby): \033[0m");
                fflush(stdout);
            }
        }
    }

    return false;
}

/* Sessione completa di gioco: piazzamento -> combattimento -> rivincita */
static void run_game_session(int sock, const char *opponent_nick, bool start_my_turn) {
    while (g_client_running) {
        Board my_board;
        if (do_ship_placement(sock, &my_board) < 0) return;

        printf("\nIn attesa che l'avversario completi il posizionamento delle sue navi...\n");
        char line[BUFFER_SIZE];
        if (read_server_line(sock, line, sizeof(line)) < 0) return;

        if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
            int res = handle_opponent_disconnected(sock);
            if (res == 1) {
                printf("\n\033[1;32mPartita conclusa per abbandono dell'avversario. Vittoria assegnata a te!\033[0m\n");
                printf("\033[1;33m[i] Nessuna rivincita possibile in quanto l'avversario si è disconnesso.\033[0m\n");
                printf("Premi Invio per tornare alla lobby principale...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return;
            } else if (res == 2) {
                host_wait_for_challenger(sock);
                return;
            }
            return;
        }

        int turn_flag = 0;
        char opp[32];
        if (sscanf(line, "COMBAT_START %d %31s", &turn_flag, opp) >= 1) {
            start_my_turn = (turn_flag == 1);
        }

        int outcome = run_combat_loop(sock, &my_board, opponent_nick, start_my_turn);

        if (outcome == OUTCOME_FORFEIT_WIN) {
            /* Vittoria a tavolino per abbandono dell'avversario: nessuna rivincita possibile */
            return;
        } else if (outcome == OUTCOME_WAIT_NEW_CHALLENGER) {
            /* Giocatore superstite ha scelto di attendere un nuovo sfidante nella stanza */
            host_wait_for_challenger(sock);
            return;
        } else if (outcome <= 0) {
            return;
        }

        /* Offerta rivincita: sia per vittoria normale che per sconfitta sul campo */
        if (!ask_rematch(sock, opponent_nick)) {
            break;
        }
    }
}

/* Attesa sfidante per l'host con multiplazione I/O tramite select() */
static void host_wait_for_challenger(int sock) {
    ui_print_header("STANZA CREATA - IN ATTESA DI UNO SFIDANTE");
    printf("Gli altri giocatori possono ora vedere la tua partita e richiedere di unirsi.\n");
    printf("Comandi disponibili:\n");
    printf("  Digita '0' o 'cancel' per annullare la stanza e tornare al menu.\n\n");

    fd_set readfds;
    char line[BUFFER_SIZE];

    while (g_client_running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            break;
        }

        /* Input da terminale (per eventuale cancellazione) */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[64];
            if (safe_read_line(input, sizeof(input)) >= 0) {
                char *t = trim_whitespace(input);
                if (strcmp(t, "0") == 0 || strcasecmp(t, "cancel") == 0) {
                    send_line(sock, "CANCEL_GAME");
                    read_server_line(sock, line, sizeof(line));
                    printf("\nPartita annullata. Ritorno alla lobby.\n");
                    return;
                }
            }
        }

        /* Messaggio dal server */
        if (FD_ISSET(sock, &readfds)) {
            if (read_server_line(sock, line, sizeof(line)) <= 0) return;

            char challenger_nick[32];
            if (sscanf(line, "CHALLENGE_REQUEST %31s", challenger_nick) == 1) {
                printf("\n\033[1;35m[!] Lo sfidante '%s' desidera unirsi alla tua partita!\033[0m\n", challenger_nick);
                printf("Accetti la sfida?\n");
                printf("  1) Accetta e avvia la partita\n");
                printf("  0) Rifiuta la richiesta\n");

                int decision = 1;
                if (get_valid_int("Scelta (1 o 0): ", 0, 1, &decision) < 0) return;

                send_line(sock, "ACCEPT_JOIN %d", decision);

                if (decision == 1) {
                    /* Leggi JOIN_ACCEPTED e START_PLACEMENT */
                    read_server_line(sock, line, sizeof(line));
                    read_server_line(sock, line, sizeof(line));
                    run_game_session(sock, challenger_nick, true);
                    return;
                } else {
                    printf("\nRichiesta rifiutata. Sei ancora in attesa di altri sfidanti...\n");
                }
            } else if (strncmp(line, "INFO", 4) == 0) {
                printf("\n%s\n", line);
            }
        }
    }
}

/* Visualizza lista partite e permette di unirsi */
static void list_and_join_game(int sock) {
    send_line(sock, "LIST_GAMES");

    char line[BUFFER_SIZE];
    if (read_server_line(sock, line, sizeof(line)) < 0) return;

    int total_games = 0;
    if (sscanf(line, "GAMES_LIST_START %d", &total_games) != 1) {
        printf("\nErrore nel formato lista partite ricevuto dal server.\n");
        return;
    }

    if (total_games == 0) {
        read_server_line(sock, line, sizeof(line)); /* Consume GAMES_LIST_END */
        printf("\n\033[1;33mNessuna partita in attesa disponibile al momento.\033[0m\n");
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
        return;
    }

    ui_print_header("PARTITE DISPONIBILI");
    printf("%-5s | %-25s | %-20s\n", "ID", "Nome Partita", "Creatore (Host)");
    printf("------+---------------------------+---------------------\n");

    int game_ids[MAX_GAMES];
    int available_count = 0;

    while (read_server_line(sock, line, sizeof(line)) > 0) {
        if (strncmp(line, "GAMES_LIST_END", 14) == 0) break;

        int gid;
        char gname[MAX_GAME_NAME_LEN], ghost[MAX_NICK_LEN];
        if (sscanf(line, "GAME_ITEM %d %31s %31s", &gid, gname, ghost) == 3) {
            printf("%-5d | %-25s | %-20s\n", gid, gname, ghost);
            game_ids[available_count++] = gid;
        }
    }

    printf("\nInserisci l'ID della partita a cui unirti (oppure 0 per annullare): ");
    int chosen_id = 0;
    while (1) {
        if (get_valid_int("", 0, MAX_GAMES, &chosen_id) < 0 || chosen_id == 0) {
            return;
        }

        bool found = false;
        for (int i = 0; i < available_count; i++) {
            if (game_ids[i] == chosen_id) {
                found = true;
                break;
            }
        }
        if (found) break;

        printf("\033[1;31m[!] L'ID %d non è presente tra le partite aperte. Riprova (0 per annullare): \033[0m", chosen_id);
    }

    send_line(sock, "JOIN_GAME %d", chosen_id);
    if (read_server_line(sock, line, sizeof(line)) < 0) return;

    if (strncmp(line, "ERR", 3) == 0) {
        printf("\n\033[1;31m[!] %s\033[0m\n", line);
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
        return;
    }

    printf("\nRichiesta di partecipazione inviata all'host! In attesa di risposta...\n");

    /* Attesa di accettazione o rifiuto */
    if (read_server_line(sock, line, sizeof(line)) < 0) return;

    if (strncmp(line, "JOIN_ACCEPTED", 13) == 0) {
        int gid;
        char host_nick[32], gname[32];
        sscanf(line, "JOIN_ACCEPTED %d %31s %31s", &gid, host_nick, gname);
        printf("\n\033[1;32m[OK] L'host %s ha ACCETTATO la tua sfida!\033[0m\n", host_nick);

        /* Leggi START_PLACEMENT */
        read_server_line(sock, line, sizeof(line));
        run_game_session(sock, host_nick, false);
    } else if (strncmp(line, "JOIN_REJECTED", 13) == 0) {
        printf("\n\033[1;31m[!] La tua richiesta di sfida è stata RIFIUTATA dall'host.\033[0m\n");
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
    }
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc > 1) host = argv[1];
    if (argc > 2) {
        if (!parse_int_in_range(argv[2], 1024, 65535, &port)) {
            fprintf(stderr, "Uso: %s [indirizzo_ip] [porta (1024-65535)]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    srand((unsigned int)time(NULL));

    ui_clear_screen();
    ui_print_banner();
    printf("Connessione al server %s:%d in corso...\n", host, port);

    g_sock_fd = connect_to_server(host, port);
    if (g_sock_fd < 0) {
        log_error("Impossibile connettersi al server su %s:%d. Verifica che il server sia avviato.", host, port);
        return EXIT_FAILURE;
    }

    printf("\033[1;32mConnesso al server con successo!\033[0m\n\n");

    /* Leggi benvenuto dal server */
    char line[BUFFER_SIZE];
    read_server_line(g_sock_fd, line, sizeof(line));
    read_server_line(g_sock_fd, line, sizeof(line));

    /* Inserimento e validazione nickname */
    char nickname[MAX_NICK_LEN];
    while (g_client_running) {
        printf("Inserisci il tuo Nickname (max 20 caratteri, senza spazi): ");
        fflush(stdout);
        if (safe_read_line(nickname, sizeof(nickname)) < 0) {
            close(g_sock_fd);
            return EXIT_FAILURE;
        }
        char *trimmed = trim_whitespace(nickname);
        if (strlen(trimmed) == 0) continue;

        bool valid = true;
        for (size_t i = 0; trimmed[i] != '\0'; i++) {
            if (isspace((unsigned char)trimmed[i])) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            printf("\033[1;31m[!] Il nickname non può contenere spazi.\033[0m\n");
            continue;
        }

        send_line(g_sock_fd, "NICK %s", trimmed);
        if (read_server_line(g_sock_fd, line, sizeof(line)) < 0) {
            close(g_sock_fd);
            return EXIT_FAILURE;
        }

        if (strncmp(line, "OK", 2) == 0) {
            printf("\033[1;32mBenvenuto, %s!\033[0m\n", trimmed);
            break;
        } else {
            printf("\033[1;31m[!] %s\033[0m\n", line);
        }
    }

    /* Menu principale della Lobby */
    while (g_client_running) {
        ui_print_header("LOBBY PRINCIPALE");
        printf("1) Crea una nuova partita\n");
        printf("2) Visualizza le partite aperte ed unisciti\n");
        printf("3) Esci dal gioco\n");

        int choice = 0;
        if (get_valid_int("Scegli un'opzione (1-3): ", 1, 3, &choice) < 0) {
            break;
        }

        if (choice == 1) {
            printf("\nInserisci un nome per la partita (o premi Invio per nome predefinito): ");
            fflush(stdout);
            char game_name[MAX_GAME_NAME_LEN];
            safe_read_line(game_name, sizeof(game_name));
            char *tg = trim_whitespace(game_name);

            if (strlen(tg) == 0) {
                send_line(g_sock_fd, "CREATE_GAME");
            } else {
                send_line(g_sock_fd, "CREATE_GAME %s", tg);
            }

            if (read_server_line(g_sock_fd, line, sizeof(line)) < 0) break;

            if (strncmp(line, "GAME_CREATED", 12) == 0) {
                host_wait_for_challenger(g_sock_fd);
            } else {
                printf("\n\033[1;31m[!] %s\033[0m\n", line);
            }
        } else if (choice == 2) {
            list_and_join_game(g_sock_fd);
        } else if (choice == 3) {
            printf("Uscita dal gioco in corso...\n");
            send_line(g_sock_fd, "QUIT");
            break;
        }
    }

    if (g_sock_fd >= 0) {
        close(g_sock_fd);
        g_sock_fd = -1;
    }
    printf("Grazie per aver giocato a Battaglia Navale!\n");
    return EXIT_SUCCESS;
}
