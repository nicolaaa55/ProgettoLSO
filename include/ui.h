#ifndef UI_H
#define UI_H

#include "protocol.h"
#include "board.h"

void ui_clear_screen(void);
void ui_print_banner(void);
void ui_print_header(const char *title);
void ui_print_boards(const Board *my_board, CellState opp_radar[BOARD_SIZE][BOARD_SIZE]);
void ui_print_single_board(const Board *my_board, const char *title);

#endif /* UI_H */
