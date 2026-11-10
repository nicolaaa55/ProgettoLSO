#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

void ui_clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void ui_print_banner(void) {
    printf("\033[1;36m");
    printf("========================================================================\n");
    printf("                 *** BATTAGLIA NAVALE - CLIENT LSO ***                  \n");
    printf("========================================================================\n");
    printf("\033[0m");
}

void ui_print_header(const char *title) {
    printf("\n\033[1;33m--- %s ---\033[0m\n", title);
}

static const char *cell_to_ansi(CellState st, bool hide_ship) {
    switch (st) {
        case CELL_EMPTY:
            return "\033[0;34m~ \033[0m"; /* Blu mare */
        case CELL_SHIP:
            if (hide_ship) return "\033[0;34m~ \033[0m";
            return "\033[1;32mS \033[0m"; /* Verde nave */
        case CELL_MISS:
            return "\033[0;37mO \033[0m"; /* Grigio/Bianco mancato */
        case CELL_HIT:
            return "\033[1;31mX \033[0m"; /* Rosso colpita */
        case CELL_SUNK:
            return "\033[1;41;37m* \033[0m"; /* Sfondo rosso affondata */
        default:
            return "? ";
    }
}

void ui_print_single_board(const Board *my_board, const char *title) {
    if (!my_board) return;

    printf("\n\033[1;32m[ %s ]\033[0m\n", title);
    printf("     ");
    for (int c = 1; c <= BOARD_SIZE; c++) {
        printf("%-2d", c);
    }
    printf("\n   +--------------------\n");

    for (int r = 0; r < BOARD_SIZE; r++) {
        printf(" %c | ", 'A' + r);
        for (int c = 0; c < BOARD_SIZE; c++) {
            printf("%s", cell_to_ansi(my_board->grid[r][c], false));
        }
        printf("\n");
    }
    printf("\n Legenda: \033[1;32mS\033[0m=Nave  \033[0;34m~\033[0m=Mare  \033[1;31mX\033[0m=Colpito  \033[0;37mO\033[0m=Mancato  \033[1;41;37m*\033[0m=Affondata\n");
}

void ui_print_boards(const Board *my_board, CellState opp_radar[BOARD_SIZE][BOARD_SIZE]) {
    printf("\n\033[1;36m");
    printf("       [ LA TUA FLOTTA ]                         [ RADAR NEMICO ]       \n");
    printf("     1 2 3 4 5 6 7 8 9 10                      1 2 3 4 5 6 7 8 9 10     \n");
    printf("   +--------------------                     +--------------------      \n");
    printf("\033[0m");

    for (int r = 0; r < BOARD_SIZE; r++) {
        /* Riga flotta giocatore */
        printf(" %c | ", 'A' + r);
        for (int c = 0; c < BOARD_SIZE; c++) {
            CellState st = my_board ? my_board->grid[r][c] : CELL_EMPTY;
            printf("%s", cell_to_ansi(st, false));
        }

        printf("                  %c | ", 'A' + r);

        /* Riga radar nemico */
        for (int c = 0; c < BOARD_SIZE; c++) {
            CellState st = opp_radar ? opp_radar[r][c] : CELL_EMPTY;
            printf("%s", cell_to_ansi(st, false));
        }
        printf("\n");
    }

    printf("\n Legenda: \033[1;32mS\033[0m=Tua Nave  \033[0;34m~\033[0m=Mare  \033[1;31mX\033[0m=Nave Colpita  \033[0;37mO\033[0m=Acqua  \033[1;41;37m*\033[0m=Affondata\n");
}
