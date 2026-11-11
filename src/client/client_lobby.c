#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "network.h"
#include "ui.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

/* Attesa sfidante per l'host con multiplazione I/O tramite select() */
void host_wait_for_challenger(int sock) {
    bool need_header = true;
    fd_set readfds;
    char line[BUFFER_SIZE];

    while (g_client_running) {
        if (need_header) {
            ui_clear_screen();
            ui_print_header("STANZA CREATA - IN ATTESA DI UNO SFIDANTE");
            printf("Gli altri giocatori possono ora vedere la tua partita e richiedere di unirsi.\n");
            printf("Comandi disponibili:\n");
            printf("  Digita '0' o 'cancel' per annullare la stanza e tornare al menu.\n\n");
            fflush(stdout);
            need_header = false;
        }

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Input da terminale (per eventuale cancellazione) */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[64];
            if (safe_read_line(input, sizeof(input)) >= 0) {
                char *t = trim_whitespace(input);
                if (strcmp(t, "0") == 0 || strcasecmp(t, "cancel") == 0) {
                    send_line(sock, "CANCEL_GAME");
                    client_read_server_line(sock, line, sizeof(line));
                    printf("\nPartita annullata. Ritorno alla lobby.\n");
                    return;
                }
            }
        }

        /* Messaggio dal server */
        if (FD_ISSET(sock, &readfds)) {
            if (client_read_server_line(sock, line, sizeof(line)) <= 0) return;

            char challenger_nick[32];
            if (sscanf(line, "CHALLENGE_REQUEST %31s", challenger_nick) == 1) {
                printf("\n\033[1;35m[!] Lo sfidante '%s' desidera unirsi alla tua partita!\033[0m\n", challenger_nick);
                printf("Accetti la sfida?\n");
                printf("  1) Accetta e avvia la partita\n");
                printf("  0) Rifiuta la richiesta\n");

                int decision = 1;
                int d_ret = client_get_valid_int_multiplex(sock, "Scelta (1 o 0): ", 0, 1, &decision);
                if (d_ret == -2) {
                    printf("\n\033[1;33m[!] Lo sfidante si è disconnesso prima della risposta.\033[0m\n");
                    printf("\033[1;32m[i] La tua stanza è di nuovo aperta e in ascolto per nuove richieste!\033[0m\n");
                    printf("Premi Invio per continuare ad attendere nella stanza...");
                    char dummy[16];
                    safe_read_line(dummy, sizeof(dummy));
                    need_header = true;
                    continue;
                }
                if (d_ret < 0) return;

                send_line(sock, "ACCEPT_JOIN %d", decision);

                if (decision == 1) {
                    if (client_read_server_line(sock, line, sizeof(line)) < 0) return;
                    if (strncmp(line, "JOIN_ACCEPTED", 13) != 0) {
                        printf("\n\033[1;31m[!] Impossibile avviare la partita: %s\033[0m\n", line);
                        printf("\033[1;32m[i] La tua stanza è di nuovo aperta e in ascolto per nuove richieste!\033[0m\n");
                        printf("Premi Invio per continuare ad attendere nella stanza...");
                        char dummy[16];
                        safe_read_line(dummy, sizeof(dummy));
                        need_header = true;
                        continue;
                    }
                    if (client_read_server_line(sock, line, sizeof(line)) < 0) return;

                    int session_res = run_game_session(sock, challenger_nick, true, true);
                    if (session_res == SESSION_WAIT_NEW_CHALLENGER) {
                        need_header = true;
                        continue;
                    }
                    return;
                } else {
                    client_read_server_line(sock, line, sizeof(line));
                    printf("\nRichiesta rifiutata. Sei ancora in attesa di altri sfidanti...\n");
                    need_header = true;
                }
            } else if (strncmp(line, "INFO", 4) == 0) {
                printf("\n%s\n", line);
                need_header = true;
            }
        }
    }
}

/* Visualizza lista partite e permette di unirsi */
void list_and_join_game(int sock) {
    send_line(sock, "LIST_GAMES");

    char line[BUFFER_SIZE];
    if (client_read_server_line(sock, line, sizeof(line)) < 0) return;

    int total_games = 0;
    if (sscanf(line, "GAMES_LIST_START %d", &total_games) != 1) {
        printf("\nErrore nel formato lista partite ricevuto dal server.\n");
        return;
    }

    if (total_games == 0) {
        client_read_server_line(sock, line, sizeof(line)); /* Consume GAMES_LIST_END */
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

    while (client_read_server_line(sock, line, sizeof(line)) > 0) {
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
    if (client_read_server_line(sock, line, sizeof(line)) < 0) return;

    if (strncmp(line, "ERR", 3) == 0) {
        printf("\n\033[1;31m[!] %s\033[0m\n", line);
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
        return;
    }

    printf("\nRichiesta di partecipazione inviata all'host! In attesa di risposta...\n");

    if (client_read_server_line(sock, line, sizeof(line)) < 0) return;

    if (strncmp(line, "JOIN_ACCEPTED", 13) == 0) {
        int gid;
        char host_nick[32], gname[32];
        sscanf(line, "JOIN_ACCEPTED %d %31s %31s", &gid, host_nick, gname);
        printf("\n\033[1;32m[OK] L'host %s ha ACCETTATO la tua sfida!\033[0m\n", host_nick);

        if (client_read_server_line(sock, line, sizeof(line)) < 0) return;
        int session_res = run_game_session(sock, host_nick, false, false);
        if (session_res == SESSION_WAIT_NEW_CHALLENGER) {
            host_wait_for_challenger(sock);
        }
    } else if (strncmp(line, "JOIN_REJECTED", 13) == 0) {
        printf("\n\033[1;31m[!] La tua richiesta di sfida è stata RIFIUTATA dall'host.\033[0m\n");
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
    } else {
        printf("\n\033[1;31m[!] %s\033[0m\n", line);
        printf("Premi Invio per tornare al menu...");
        char dummy[16];
        safe_read_line(dummy, sizeof(dummy));
    }
}
