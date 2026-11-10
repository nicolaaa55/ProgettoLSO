#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>

int safe_read_line(char *buf, size_t size) {
    if (!buf || size == 0) return -1;

    if (fgets(buf, (int)size, stdin) == NULL) {
        return -1; /* EOF o errore */
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        if (len > 1 && buf[len - 2] == '\r') {
            buf[len - 2] = '\0';
        }
    } else if (len == size - 1) {
        /* L'input era più lungo del buffer: scartiamo i caratteri rimanenti fino a newline */
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);
    }

    return 0;
}

char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void safe_str_copy(char *dest, const char *src, size_t dest_size) {
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

bool parse_int_in_range(const char *str, int min, int max, int *out_val) {
    if (!str) return false;
    char *endptr = NULL;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str) return false;
    while (*endptr != '\0') {
        if (!isspace((unsigned char)*endptr)) return false;
        endptr++;
    }

    if (val < min || val > max) return false;
    if (out_val) *out_val = (int)val;
    return true;
}

bool parse_coord_str(const char *str, int *row, int *col) {
    if (!str || !row || !col) return false;

    char temp[64];
    safe_str_copy(temp, str, sizeof(temp));
    char *s = trim_whitespace(temp);
    if (!s || strlen(s) == 0) return false;

    /* Formato 1: Due numeri separati da spazio (es. "0 4" o "1 5") */
    int r_num, c_num;
    if (sscanf(s, "%d %d", &r_num, &c_num) == 2) {
        /* Se l'utente usa 1-based (1-10) o 0-based (0-9) */
        if (r_num >= 1 && r_num <= BOARD_SIZE && c_num >= 1 && c_num <= BOARD_SIZE) {
            *row = r_num - 1;
            *col = c_num - 1;
            return true;
        } else if (r_num >= 0 && r_num < BOARD_SIZE && c_num >= 0 && c_num < BOARD_SIZE) {
            *row = r_num;
            *col = c_num;
            return true;
        }
    }

    /* Formato 2: Notazione navale standard, es. "B4", "b4", "A10", "10A", "B 4" */
    char letter = 0;
    int number = -1;

    for (size_t i = 0; s[i] != '\0'; i++) {
        if (isalpha((unsigned char)s[i])) {
            if (letter == 0) {
                letter = toupper((unsigned char)s[i]);
            } else {
                return false; /* Più di una lettera */
            }
        } else if (isdigit((unsigned char)s[i])) {
            if (number == -1) {
                number = (int)strtol(&s[i], NULL, 10);
                /* Avanza fino alla fine della cifra */
                while (isdigit((unsigned char)s[i + 1])) i++;
            }
        }
    }

    if (letter >= 'A' && letter < 'A' + BOARD_SIZE && number >= 1 && number <= BOARD_SIZE) {
        *row = letter - 'A';
        *col = number - 1;
        return true;
    }

    return false;
}

int get_valid_int(const char *prompt, int min, int max, int *out_val) {
    char input_buf[128];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (safe_read_line(input_buf, sizeof(input_buf)) < 0) {
            return -1; /* EOF */
        }
        char *trimmed = trim_whitespace(input_buf);
        if (strlen(trimmed) == 0) {
            continue;
        }
        if (parse_int_in_range(trimmed, min, max, out_val)) {
            return 0;
        }
        printf("\033[1;31m[!] Input non valido. Inserire un numero compreso tra %d e %d.\033[0m\n", min, max);
    }
}

int get_valid_coords(const char *prompt, int *row, int *col) {
    char input_buf[128];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (safe_read_line(input_buf, sizeof(input_buf)) < 0) {
            return -1; /* EOF */
        }
        char *trimmed = trim_whitespace(input_buf);
        if (strlen(trimmed) == 0) {
            continue;
        }
        if (parse_coord_str(trimmed, row, col)) {
            return 0;
        }
        printf("\033[1;31m[!] Coordinate non valide. Usa il formato LetteraNumero (es. B4, J10, A1) tra A e %c e tra 1 e %d.\033[0m\n",
               'A' + BOARD_SIZE - 1, BOARD_SIZE);
    }
}

static void print_timestamp(FILE *stream) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    fprintf(stream, "[%s] ", buf);
}

void log_info(const char *fmt, ...) {
    print_timestamp(stdout);
    printf("\033[1;32m[INFO]\033[0m ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_warn(const char *fmt, ...) {
    print_timestamp(stdout);
    printf("\033[1;33m[WARN]\033[0m ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_error(const char *fmt, ...) {
    print_timestamp(stderr);
    fprintf(stderr, "\033[1;31m[ERROR]\033[0m ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}
