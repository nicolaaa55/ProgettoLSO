#ifndef BOARD_H
#define BOARD_H

#include "protocol.h"
#include <stdbool.h>

typedef struct {
    char name[32];
    int size;
    bool placed;
    int row;
    int col;
    ShipDirection dir;
    int hits;
    bool sunk;
} Ship;

typedef struct {
    CellState grid[BOARD_SIZE][BOARD_SIZE];
    int ship_map[BOARD_SIZE][BOARD_SIZE]; /* Indice della nave o -1 */
    Ship ships[NUM_SHIPS];
    int ships_placed;
    int ships_alive;
} Board;

/* Inizializzazione e gestione griglia */
void board_init(Board *b);
void board_reset(Board *b);

/* Posizionamento navi */
bool board_can_place_ship(const Board *b, int ship_idx, int row, int col, ShipDirection dir, const char **err_msg);
bool board_place_ship(Board *b, int ship_idx, int row, int col, ShipDirection dir);
void board_auto_place_all(Board *b);

/* Risoluzione colpi */
ShotResult board_receive_shot(Board *b, int row, int col, int *out_ship_idx);
bool board_all_sunk(const Board *b);

#endif /* BOARD_H */
