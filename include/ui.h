#ifndef UI_H
#define UI_H

#include "protocol.h"
#include "board.h"

void ui_clear_screen(void);
void ui_print_banner(void);
void ui_print_header(const char *title);
void ui_print_boards(const Board *my_board, CellState opp_radar[BOARD_SIZE][BOARD_SIZE]);
void ui_print_single_board(const Board *my_board, const char *title);

/* Aggiornamento grafico delle celle per navi affondate */
void ui_mark_sunk_on_radar(CellState opp_radar[BOARD_SIZE][BOARD_SIZE],
                           int shot_r, int shot_c,
                           int sunk_r, int sunk_c, int sunk_len, int sunk_dir);

void ui_mark_sunk_on_board(Board *my_board,
                           int shot_r, int shot_c,
                           int sunk_r, int sunk_c, int sunk_len, int sunk_dir);

#endif /* UI_H */
