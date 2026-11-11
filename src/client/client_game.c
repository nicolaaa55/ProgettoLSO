#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "network.h"
#include "ui.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <ctype.h>
#include <errno.h>

/* Esiti della fase di combattimento */
enum CombatOutcome {
    OUTCOME_ERROR = 0,
    OUTCOME_VICTORY = 1,
    OUTCOME_DEFEAT = 2,
    OUTCOME_FORFEIT_WIN = 3,
    OUTCOME_WAIT_NEW_CHALLENGER = 4
};

/* Selezione interattiva dell'orientamento e direzione della nave */
static int select_ship_direction(int sock, const Board *my_board, int ship_idx, int r, int c,
                                 ShipDirection *out_dir, char *out_dir_c) {
    char dir_buf[32];
    while (g_client_running) {
        printf("Orientamento [H = Orizzontale, V = Verticale]: ");
        fflush(stdout);
        int dr = client_safe_read_line_multiplex(sock, dir_buf, sizeof(dir_buf));
        if (dr == -2) return -2;
        if (dr < 0) return -1;
        char *td = trim_whitespace(dir_buf);
        if (!td || strlen(td) == 0) continue;
        char choice = (char)toupper((unsigned char)td[0]);

        if (choice == 'H') {
            bool can_right = board_can_place_ship(my_board, ship_idx, r, c, DIR_RIGHT, NULL);
            bool can_left  = board_can_place_ship(my_board, ship_idx, r, c, DIR_LEFT, NULL);

            if (can_right && can_left) {
                while (1) {
                    printf("  Direzione orizzontale [D = Verso Destra, S = Verso Sinistra]: ");
                    fflush(stdout);
                    int sdr = client_safe_read_line_multiplex(sock, dir_buf, sizeof(dir_buf));
                    if (sdr == -2) return -2;
                    if (sdr < 0) return -1;
                    char *sub = trim_whitespace(dir_buf);
                    if (sub && (toupper((unsigned char)sub[0]) == 'D' || toupper((unsigned char)sub[0]) == 'R')) {
                        *out_dir = DIR_RIGHT;
                        *out_dir_c = 'D';
                        return 1;
                    } else if (sub && (toupper((unsigned char)sub[0]) == 'S' || toupper((unsigned char)sub[0]) == 'L')) {
                        *out_dir = DIR_LEFT;
                        *out_dir_c = 'S';
                        return 1;
                    }
                    printf("\033[1;31m  [!] Inserisci D per Destra o S per Sinistra.\033[0m\n");
                }
            } else if (can_right) {
                printf("\033[1;36m  [i] Verso sinistra non c'è spazio: posizionamento automatico verso Destra.\033[0m\n");
                *out_dir = DIR_RIGHT;
                *out_dir_c = 'D';
                return 1;
            } else if (can_left) {
                printf("\033[1;36m  [i] Verso destra non c'è spazio: posizionamento automatico verso Sinistra.\033[0m\n");
                *out_dir = DIR_LEFT;
                *out_dir_c = 'S';
                return 1;
            } else {
                printf("\033[1;31m[!] Nessuna direzione orizzontale valida (Destra o Sinistra) da questa coordinata.\033[0m\n");
                return 0;
            }
        } else if (choice == 'V') {
            bool can_down = board_can_place_ship(my_board, ship_idx, r, c, DIR_DOWN, NULL);
            bool can_up   = board_can_place_ship(my_board, ship_idx, r, c, DIR_UP, NULL);

            if (can_down && can_up) {
                while (1) {
                    printf("  Direzione verticale [B = Verso il Basso, A = Verso l'Alto]: ");
                    fflush(stdout);
                    int sdr = client_safe_read_line_multiplex(sock, dir_buf, sizeof(dir_buf));
                    if (sdr == -2) return -2;
                    if (sdr < 0) return -1;
                    char *sub = trim_whitespace(dir_buf);
                    if (sub && (toupper((unsigned char)sub[0]) == 'B' || toupper((unsigned char)sub[0]) == 'V')) {
                        *out_dir = DIR_DOWN;
                        *out_dir_c = 'B';
                        return 1;
                    } else if (sub && (toupper((unsigned char)sub[0]) == 'A' || toupper((unsigned char)sub[0]) == 'U')) {
                        *out_dir = DIR_UP;
                        *out_dir_c = 'A';
                        return 1;
                    }
                    printf("\033[1;31m  [!] Inserisci B per Basso o A per Alto.\033[0m\n");
                }
            } else if (can_down) {
                printf("\033[1;36m  [i] Verso l'alto non c'è spazio: posizionamento automatico verso il Basso.\033[0m\n");
                *out_dir = DIR_DOWN;
                *out_dir_c = 'B';
                return 1;
            } else if (can_up) {
                printf("\033[1;36m  [i] Verso il basso non c'è spazio: posizionamento automatico verso l'Alto.\033[0m\n");
                *out_dir = DIR_UP;
                *out_dir_c = 'A';
                return 1;
            } else {
                printf("\033[1;31m[!] Nessuna direzione verticale valida (In alto o In basso) da questa coordinata.\033[0m\n");
                return 0;
            }
        } else if (choice == 'D') {
            *out_dir = DIR_RIGHT; *out_dir_c = 'D'; return 1;
        } else if (choice == 'S') {
            *out_dir = DIR_LEFT; *out_dir_c = 'S'; return 1;
        } else if (choice == 'B') {
            *out_dir = DIR_DOWN; *out_dir_c = 'B'; return 1;
        } else if (choice == 'A') {
            *out_dir = DIR_UP; *out_dir_c = 'A'; return 1;
        } else {
            printf("\033[1;31m[!] Scelta non valida. Inserisci H per orizzontale o V per verticale.\033[0m\n");
        }
    }
    return -1;
}

/* Gestione piazzamento navi (Manuale o Automatico) */
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
    int m_ret = client_get_valid_int_multiplex(sock, "Scelta (1 o 2): ", 1, 2, &mode);
    if (m_ret == -2) return -2;
    if (m_ret < 0) return -1;

    if (mode == 2) {
        board_auto_place_all(my_board);
        printf("\nPosizionamento automatico generato con successo!\n");
        ui_print_single_board(my_board, "LA TUA FLOTTA GENERATA");

        for (int i = 0; i < NUM_SHIPS; i++) {
            char dir_c = (my_board->ships[i].dir == DIR_VERTICAL) ? 'V' : 'H';
            send_line(sock, "PLACE_SHIP %d %d %d %c", i, my_board->ships[i].row, my_board->ships[i].col, dir_c);
            char resp[BUFFER_SIZE];
            if (client_read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
            if (strncmp(resp, "OPPONENT_DISCONNECTED", 21) == 0 ||
                strncmp(resp, "ERR_GAME_CANCELLED", 18) == 0) {
                safe_str_copy(g_last_server_event, resp, sizeof(g_last_server_event));
                return -2;
            }
        }

        char resp[BUFFER_SIZE];
        if (client_read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
        if (strncmp(resp, "OPPONENT_DISCONNECTED", 21) == 0 ||
            strncmp(resp, "ERR_GAME_CANCELLED", 18) == 0) {
            safe_str_copy(g_last_server_event, resp, sizeof(g_last_server_event));
            return -2;
        }
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
            int cr = client_get_valid_coords_multiplex(sock, "Coordinata di partenza (es. B4, A1, J10): ", &r, &c);
            if (cr == -2) return -2;
            if (cr < 0) return -1;

            ShipDirection dir = DIR_HORIZONTAL;
            char dir_c = 'H';
            int s_ret = select_ship_direction(sock, my_board, i, r, c, &dir, &dir_c);
            if (s_ret == -2) return -2;
            if (s_ret < 0) return -1;
            if (s_ret == 0) continue;

            const char *err = NULL;
            if (!board_can_place_ship(my_board, i, r, c, dir, &err)) {
                printf("\n\033[1;31m[!] Posizionamento non valido: %s\033[0m\n", err ? err : "errore sconosciuto");
                continue;
            }

            send_line(sock, "PLACE_SHIP %d %d %d %c", i, r, c, dir_c);

            char resp[BUFFER_SIZE];
            if (client_read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
            if (strncmp(resp, "OPPONENT_DISCONNECTED", 21) == 0 ||
                strncmp(resp, "ERR_GAME_CANCELLED", 18) == 0) {
                safe_str_copy(g_last_server_event, resp, sizeof(g_last_server_event));
                return -2;
            }

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
    if (client_read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
    if (strncmp(resp, "OPPONENT_DISCONNECTED", 21) == 0 ||
        strncmp(resp, "ERR_GAME_CANCELLED", 18) == 0) {
        safe_str_copy(g_last_server_event, resp, sizeof(g_last_server_event));
        return -2;
    }
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
    if (client_read_server_line(sock, resp, sizeof(resp)) < 0) return -1;
    printf("\n%s\n", resp);

    if (choice == 1) {
        printf("\n\033[1;32mPartita conclusa per abbandono dell'avversario. Vittoria assegnata a te!\033[0m\n");
        printf("\033[1;33m[i] Nessuna rivincita possibile in quanto l'avversario si è disconnesso.\033[0m\n");
        printf("Premi Invio per tornare alla lobby principale...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
    } else if (choice == 2) {
        printf("\n\033[1;32m[i] Hai scelto di attendere un nuovo sfidante nella stanza.\033[0m\n");
        printf("Premi Invio per continuare ad attendere...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
    }

    return choice;
}

/* Elabora e visualizza l'esito di un colpo navale (nostro o nemico) */
static void handle_combat_shot_result(const char *line, const char *opponent_nick, bool is_my_shot,
                                      Board *my_board, CellState opp_radar[BOARD_SIZE][BOARD_SIZE]) {
    char shooter[32], outcome[32], ship_name[32];
    int shot_r = -1, shot_c = -1, next_turn = 0;
    int sunk_r = -1, sunk_c = -1, sunk_len = 0, sunk_dir = 0;
    int parsed = sscanf(line, "SHOT_RESULT %31s %d %d %31s %31s %d %d %d %d %d",
                        shooter, &shot_r, &shot_c, outcome, ship_name, &next_turn,
                        &sunk_r, &sunk_c, &sunk_len, &sunk_dir);
    if (parsed < 4) return;

    if (is_my_shot) {
        if (strcmp(outcome, "MISS") == 0) {
            opp_radar[shot_r][shot_c] = CELL_MISS;
            printf("\n\033[0;37m>>> ACQUA! Colpo a vuoto in %c%d <<<\033[0m\n", 'A' + shot_r, shot_c + 1);
        } else if (strcmp(outcome, "HIT") == 0) {
            opp_radar[shot_r][shot_c] = CELL_HIT;
            printf("\n\033[1;31m>>> COLPITO! Nave nemica centrata in %c%d! <<<\033[0m\n", 'A' + shot_r, shot_c + 1);
        } else if (strcmp(outcome, "SUNK") == 0) {
            ui_mark_sunk_on_radar(opp_radar, shot_r, shot_c, sunk_r, sunk_c, sunk_len, sunk_dir);
            printf("\n\033[1;41;37m>>> COLPITO E AFFONDATO! Hai affondato: %s! <<<\033[0m\n", ship_name);
        }
    } else {
        if (strcmp(outcome, "MISS") == 0) {
            if (my_board->grid[shot_r][shot_c] == CELL_EMPTY) {
                my_board->grid[shot_r][shot_c] = CELL_MISS;
            }
            printf("\n\033[0;37m>>> %s ha sparato in %c%d: ACQUA! <<<\033[0m\n", opponent_nick, 'A' + shot_r, shot_c + 1);
        } else if (strcmp(outcome, "HIT") == 0) {
            my_board->grid[shot_r][shot_c] = CELL_HIT;
            printf("\n\033[1;31m>>> ALLARME! %s ha COLPITO una tua nave in %c%d! <<<\033[0m\n", opponent_nick, 'A' + shot_r, shot_c + 1);
        } else if (strcmp(outcome, "SUNK") == 0) {
            ui_mark_sunk_on_board(my_board, shot_r, shot_c, sunk_r, sunk_c, sunk_len, sunk_dir);
            printf("\n\033[1;41;37m>>> AFFONDATA! La tua nave '%s' è stata affondata da %s! <<<\033[0m\n", ship_name, opponent_nick);
        }
    }
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
                int c_ret = client_get_valid_coords_multiplex(sock, "Dichiara coordinate bersaglio di fuoco (es. B4): ", &r, &c);
                if (c_ret == -2) {
                    int res = handle_opponent_disconnected(sock);
                    if (res == 1) return OUTCOME_FORFEIT_WIN;
                    if (res == 2) return OUTCOME_WAIT_NEW_CHALLENGER;
                    return OUTCOME_ERROR;
                }
                if (c_ret < 0) {
                    return OUTCOME_ERROR;
                }
                if (opp_radar[r][c] != CELL_EMPTY) {
                    printf("\033[1;33m[!] Hai già sparato in %c%d! Scegli un'altra cella.\033[0m\n", 'A' + r, c + 1);
                    continue;
                }
                break;
            }

            send_line(sock, "FIRE %d %d", r, c);

            if (client_read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) return OUTCOME_FORFEIT_WIN;
                if (res == 2) return OUTCOME_WAIT_NEW_CHALLENGER;
                return OUTCOME_ERROR;
            }

            handle_combat_shot_result(line, opponent_nick, true, my_board, opp_radar);

            if (client_read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) return OUTCOME_FORFEIT_WIN;
                if (res == 2) return OUTCOME_WAIT_NEW_CHALLENGER;
                return OUTCOME_ERROR;
            }

            if (strncmp(line, "GAME_OVER", 9) == 0) {
                ui_print_boards(my_board, opp_radar);
                printf("\n\033[1;32m");
                printf("************************************************************\n");
                printf("               VITTORIA! HAI VINTO LA PARTITA!              \n");
                printf("************************************************************\n");
                printf("\033[0m\n");
                return OUTCOME_VICTORY;
            } else if (strncmp(line, "WAIT_TURN", 9) == 0) {
                my_turn = false;
            }
        } else {
            printf("\n\033[1;33m--- In attesa del colpo di %s... ---\033[0m\n", opponent_nick);

            if (client_read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) return OUTCOME_FORFEIT_WIN;
                if (res == 2) return OUTCOME_WAIT_NEW_CHALLENGER;
                return OUTCOME_ERROR;
            }

            handle_combat_shot_result(line, opponent_nick, false, my_board, opp_radar);

            if (client_read_server_line(sock, line, sizeof(line)) < 0) return OUTCOME_ERROR;

            if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                int res = handle_opponent_disconnected(sock);
                if (res == 1) return OUTCOME_FORFEIT_WIN;
                if (res == 2) return OUTCOME_WAIT_NEW_CHALLENGER;
                return OUTCOME_ERROR;
            }

            if (strncmp(line, "GAME_OVER", 9) == 0) {
                ui_print_boards(my_board, opp_radar);
                printf("\n\033[1;31m");
                printf("************************************************************\n");
                printf("           SCONFITTA! TUTTE LE TUE NAVI SONO AFFONDATE      \n");
                printf("************************************************************\n");
                printf("\033[0m\n");
                return OUTCOME_DEFEAT;
            } else if (strncmp(line, "YOUR_TURN", 9) == 0) {
                my_turn = true;
            }
        }
    }
    return OUTCOME_ERROR;
}

/* Gestione richiesta di rivincita a fine partita */
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

        if (FD_ISSET(sock, &readfds)) {
            if (client_read_server_line(sock, line, sizeof(line)) < 0) {
                return false;
            }

            if (strncmp(line, "REMATCH_OFFERED", 15) == 0) {
                printf("\n\n\033[1;32m>>> [SFIDA RICEVUTA] %s desidera una rivincita! <<<\033[0m\n",
                       opponent_nick ? opponent_nick : "L'avversario");
                printf("Vuoi accettare la sfida? (1 = Accetta, 2 = Rifiuta ed esci): ");
                fflush(stdout);
            } else if (strncmp(line, "REMATCH_RESULT ACCEPTED", 23) == 0) {
                printf("\n\033[1;32m[OK] Entrambi avete accettato la rivincita! Si ricomincia!\033[0m\n");
                client_read_server_line(sock, line, sizeof(line));
                return true;
            } else if (strncmp(line, "REMATCH_RESULT DECLINED", 23) == 0) {
                printf("\n\033[1;33m[!] %s ha rifiutato la rivincita / abbandonato la stanza. Ritorno alla lobby.\033[0m\n",
                       opponent_nick ? opponent_nick : "L'avversario");
                printf("Premi Invio per tornare alla lobby principale...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return false;
            } else if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0) {
                printf("\n\033[1;31m[!] L'avversario si è disconnesso dal server. Ritorno alla lobby.\033[0m\n");
                printf("Premi Invio per tornare alla lobby principale...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return false;
            } else if (strncmp(line, "OK", 2) == 0) {
                continue;
            }
        }

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
                client_read_server_line(sock, resp, sizeof(resp));
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
int run_game_session(int sock, const char *opponent_nick, bool start_my_turn, bool is_host) {
    while (g_client_running) {
        g_last_server_event[0] = '\0';
        Board my_board;
        int p_res = do_ship_placement(sock, &my_board);
        if (p_res == -2) {
            if (is_host) {
                printf("\n\033[1;33m[!] Lo sfidante '%s' si è disconnesso prima dell'inizio del combattimento.\033[0m\n", opponent_nick);
                printf("\033[1;32m[i] La tua stanza è di nuovo aperta e in ascolto per nuove richieste!\033[0m\n");
                printf("Premi Invio per continuare ad attendere nella stanza...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return SESSION_WAIT_NEW_CHALLENGER;
            } else {
                printf("\n\033[1;31m[!] L'host '%s' si è disconnesso prima dell'inizio del combattimento.\033[0m\n", opponent_nick);
                printf("Premi Invio per tornare alla lobby principale...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return SESSION_LOBBY;
            }
        }
        if (p_res < 0) return SESSION_ERROR;

        printf("\nIn attesa che l'avversario completi il posizionamento delle sue navi...\n");
        char line[BUFFER_SIZE];
        if (client_read_server_line(sock, line, sizeof(line)) < 0) return SESSION_ERROR;

        if (strncmp(line, "OPPONENT_DISCONNECTED", 21) == 0 ||
            strncmp(line, "ERR_GAME_CANCELLED", 18) == 0) {
            if (is_host) {
                printf("\n\033[1;33m[!] Lo sfidante '%s' si è disconnesso prima dell'inizio del combattimento.\033[0m\n", opponent_nick);
                printf("\033[1;32m[i] La tua stanza è di nuovo aperta e in ascolto per nuove richieste!\033[0m\n");
                printf("Premi Invio per continuare ad attendere nella stanza...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return SESSION_WAIT_NEW_CHALLENGER;
            } else {
                printf("\n\033[1;31m[!] L'host '%s' si è disconnesso prima dell'inizio del combattimento.\033[0m\n", opponent_nick);
                printf("Premi Invio per tornare alla lobby principale...");
                char dummy[16];
                safe_read_line(dummy, sizeof(dummy));
                return SESSION_LOBBY;
            }
        }

        int turn_flag = 0;
        char opp[32];
        if (sscanf(line, "COMBAT_START %d %31s", &turn_flag, opp) >= 1) {
            start_my_turn = (turn_flag == 1);
        }

        int outcome = run_combat_loop(sock, &my_board, opponent_nick, start_my_turn);

        if (outcome == OUTCOME_FORFEIT_WIN) {
            return SESSION_LOBBY;
        } else if (outcome == OUTCOME_WAIT_NEW_CHALLENGER) {
            return SESSION_WAIT_NEW_CHALLENGER;
        } else if (outcome <= 0) {
            return SESSION_ERROR;
        }

        if (!ask_rematch(sock, opponent_nick)) {
            return SESSION_LOBBY;
        }
    }
    return SESSION_LOBBY;
}
