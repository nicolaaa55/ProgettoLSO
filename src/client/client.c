#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "network.h"
#include "ui.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

volatile sig_atomic_t g_client_running = 1;
int g_sock_fd = -1;
char g_last_server_event[BUFFER_SIZE] = "";

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

/* Attende e legge una linea dal server, gestendo eventuali disconnessioni */
int client_read_server_line(int sock, char *buf, size_t size) {
    int n = recv_line(sock, buf, size);
    if (n <= 0) {
        printf("\n\033[1;31m[!] Connessione con il server interrotta.\033[0m\n");
        return -1;
    }
    return n;
}

/* Lettura riga con multiplexing tra tastiera e socket di rete */
int client_safe_read_line_multiplex(int sock, char *buf, size_t size) {
    fd_set readfds;
    while (g_client_running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (FD_ISSET(sock, &readfds)) {
            char smsg[BUFFER_SIZE];
            int n = recv_line(sock, smsg, sizeof(smsg));
            if (n <= 0) return -1;
            safe_str_copy(g_last_server_event, smsg, sizeof(g_last_server_event));
            if (strncmp(smsg, "OPPONENT_DISCONNECTED", 21) == 0 ||
                strncmp(smsg, "INFO", 4) == 0 ||
                strncmp(smsg, "ERR_GAME_CANCELLED", 18) == 0 ||
                strncmp(smsg, "ERR_", 4) == 0) {
                return -2;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            return safe_read_line(buf, size);
        }
    }
    return -1;
}

int client_get_valid_int_multiplex(int sock, const char *prompt, int min, int max, int *out_val) {
    char buf[64];
    while (g_client_running) {
        printf("%s", prompt);
        fflush(stdout);
        int r = client_safe_read_line_multiplex(sock, buf, sizeof(buf));
        if (r == -2) return -2;
        if (r < 0) return -1;
        int val;
        if (parse_int_in_range(buf, min, max, &val)) {
            *out_val = val;
            return 0;
        }
        printf("\033[1;31m[!] Valore non valido. Inserisci un numero tra %d e %d.\033[0m\n", min, max);
    }
    return -1;
}

int client_get_valid_coords_multiplex(int sock, const char *prompt, int *out_row, int *out_col) {
    char buf[64];
    while (g_client_running) {
        printf("%s", prompt);
        fflush(stdout);
        int r = client_safe_read_line_multiplex(sock, buf, sizeof(buf));
        if (r == -2) return -2;
        if (r < 0) return -1;
        int row, col;
        if (parse_coord_str(buf, &row, &col)) {
            *out_row = row;
            *out_col = col;
            return 0;
        }
        printf("\033[1;31m[!] Formato coordinata non valido. Usa es. B4, A1, J10 (righe A-J, colonne 1-10).\033[0m\n");
    }
    return -1;
}

static bool setup_nickname(int sock) {
    char nickname[MAX_NICK_LEN];
    char line[BUFFER_SIZE];

    while (g_client_running) {
        printf("Inserisci il tuo Nickname (max 20 caratteri, senza spazi): ");
        fflush(stdout);
        if (safe_read_line(nickname, sizeof(nickname)) < 0) {
            return false;
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

        send_line(sock, "NICK %s", trimmed);
        if (client_read_server_line(sock, line, sizeof(line)) < 0) {
            return false;
        }

        if (strncmp(line, "OK", 2) == 0) {
            printf("\033[1;32mBenvenuto, %s!\033[0m\n", trimmed);
            return true;
        } else {
            printf("\033[1;31m[!] %s\033[0m\n", line);
        }
    }
    return false;
}

static void main_menu_loop(int sock) {
    char line[BUFFER_SIZE];

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
                send_line(sock, "CREATE_GAME");
            } else {
                send_line(sock, "CREATE_GAME %s", tg);
            }

            if (client_read_server_line(sock, line, sizeof(line)) < 0) break;

            if (strncmp(line, "GAME_CREATED", 12) == 0) {
                host_wait_for_challenger(sock);
            } else {
                printf("\n\033[1;31m[!] %s\033[0m\n", line);
            }
        } else if (choice == 2) {
            list_and_join_game(sock);
        } else if (choice == 3) {
            printf("Uscita dal gioco in corso...\n");
            send_line(sock, "QUIT");
            break;
        }
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
    client_read_server_line(g_sock_fd, line, sizeof(line));
    client_read_server_line(g_sock_fd, line, sizeof(line));

    if (!setup_nickname(g_sock_fd)) {
        close(g_sock_fd);
        return EXIT_FAILURE;
    }

    main_menu_loop(g_sock_fd);

    if (g_sock_fd >= 0) {
        close(g_sock_fd);
        g_sock_fd = -1;
    }
    printf("Grazie per aver giocato a Battaglia Navale!\n");
    return EXIT_SUCCESS;
}
