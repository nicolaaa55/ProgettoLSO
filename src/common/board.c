#include "board.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>

void board_init(Board *b) {
    if (!b) return;
    memset(b, 0, sizeof(Board));

    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            b->grid[r][c] = CELL_EMPTY;
            b->ship_map[r][c] = -1;
        }
    }

    for (int i = 0; i < NUM_SHIPS; i++) {
        safe_str_copy(b->ships[i].name, SHIP_SPECS[i].name, sizeof(b->ships[i].name));
        b->ships[i].size = SHIP_SPECS[i].size;
        b->ships[i].placed = false;
        b->ships[i].hits = 0;
        b->ships[i].sunk = false;
    }

    b->ships_placed = 0;
    b->ships_alive = NUM_SHIPS;
}

void board_reset(Board *b) {
    board_init(b);
}

bool board_can_place_ship(const Board *b, int ship_idx, int row, int col, ShipDirection dir, const char **err_msg) {
    if (!b) {
        if (err_msg) *err_msg = "Griglia non valida.";
        return false;
    }
    if (ship_idx < 0 || ship_idx >= NUM_SHIPS) {
        if (err_msg) *err_msg = "Indice nave non valido.";
        return false;
    }
    if (b->ships[ship_idx].placed) {
        if (err_msg) *err_msg = "Questa nave è già stata posizionata.";
        return false;
    }
    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
        if (err_msg) *err_msg = "Coordinate fuori dai limiti della griglia.";
        return false;
    }

    int size = b->ships[ship_idx].size;

    if (dir == DIR_RIGHT || dir == DIR_HORIZONTAL) {
        if (col + size > BOARD_SIZE) {
            if (err_msg) *err_msg = "La nave fuoriesce a destra dal bordo della griglia.";
            return false;
        }
        for (int c = col; c < col + size; c++) {
            if (b->grid[row][c] != CELL_EMPTY) {
                if (err_msg) *err_msg = "Sovrapposizione con un'altra nave presente sulla griglia.";
                return false;
            }
        }
    } else if (dir == DIR_LEFT) {
        if (col - size + 1 < 0) {
            if (err_msg) *err_msg = "La nave fuoriesce a sinistra dal bordo della griglia.";
            return false;
        }
        for (int c = col; c > col - size; c--) {
            if (b->grid[row][c] != CELL_EMPTY) {
                if (err_msg) *err_msg = "Sovrapposizione con un'altra nave presente sulla griglia.";
                return false;
            }
        }
    } else if (dir == DIR_DOWN || dir == DIR_VERTICAL) {
        if (row + size > BOARD_SIZE) {
            if (err_msg) *err_msg = "La nave fuoriesce in basso dal bordo della griglia.";
            return false;
        }
        for (int r = row; r < row + size; r++) {
            if (b->grid[r][col] != CELL_EMPTY) {
                if (err_msg) *err_msg = "Sovrapposizione con un'altra nave presente sulla griglia.";
                return false;
            }
        }
    } else if (dir == DIR_UP) {
        if (row - size + 1 < 0) {
            if (err_msg) *err_msg = "La nave fuoriesce in alto dal bordo della griglia.";
            return false;
        }
        for (int r = row; r > row - size; r--) {
            if (b->grid[r][col] != CELL_EMPTY) {
                if (err_msg) *err_msg = "Sovrapposizione con un'altra nave presente sulla griglia.";
                return false;
            }
        }
    } else {
        if (err_msg) *err_msg = "Orientamento non valido.";
        return false;
    }

    return true;
}

bool board_place_ship(Board *b, int ship_idx, int row, int col, ShipDirection dir) {
    const char *err = NULL;
    if (!board_can_place_ship(b, ship_idx, row, col, dir, &err)) {
        return false;
    }

    int size = b->ships[ship_idx].size;
    int start_row = row;
    int start_col = col;
    ShipDirection norm_dir = DIR_HORIZONTAL;

    if (dir == DIR_RIGHT || dir == DIR_HORIZONTAL) {
        start_row = row;
        start_col = col;
        norm_dir = DIR_HORIZONTAL;
        for (int c = col; c < col + size; c++) {
            b->grid[row][c] = CELL_SHIP;
            b->ship_map[row][c] = ship_idx;
        }
    } else if (dir == DIR_LEFT) {
        start_row = row;
        start_col = col - size + 1;
        norm_dir = DIR_HORIZONTAL;
        for (int c = col; c > col - size; c--) {
            b->grid[row][c] = CELL_SHIP;
            b->ship_map[row][c] = ship_idx;
        }
    } else if (dir == DIR_DOWN || dir == DIR_VERTICAL) {
        start_row = row;
        start_col = col;
        norm_dir = DIR_VERTICAL;
        for (int r = row; r < row + size; r++) {
            b->grid[r][col] = CELL_SHIP;
            b->ship_map[r][col] = ship_idx;
        }
    } else if (dir == DIR_UP) {
        start_row = row - size + 1;
        start_col = col;
        norm_dir = DIR_VERTICAL;
        for (int r = row; r > row - size; r--) {
            b->grid[r][col] = CELL_SHIP;
            b->ship_map[r][col] = ship_idx;
        }
    }

    /* Memorizziamo sempre le coordinate canoniche (vertice in alto a sinistra) */
    b->ships[ship_idx].row = start_row;
    b->ships[ship_idx].col = start_col;
    b->ships[ship_idx].dir = norm_dir;
    b->ships[ship_idx].placed = true;
    b->ships_placed++;

    return true;
}

void board_auto_place_all(Board *b) {
    if (!b) return;

    for (int i = 0; i < NUM_SHIPS; i++) {
        if (b->ships[i].placed) continue;

        int attempts = 0;
        bool placed = false;
        while (!placed && attempts < 1000) {
            attempts++;
            int r = rand() % BOARD_SIZE;
            int c = rand() % BOARD_SIZE;
            ShipDirection dir = (ShipDirection)(rand() % 4);

            if (board_place_ship(b, i, r, c, dir)) {
                placed = true;
            }
        }
    }
}

ShotResult board_receive_shot(Board *b, int row, int col, int *out_ship_idx) {
    if (out_ship_idx) *out_ship_idx = -1;

    if (!b || row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
        return SHOT_INVALID;
    }

    CellState current = b->grid[row][col];
    if (current == CELL_HIT || current == CELL_MISS || current == CELL_SUNK) {
        return SHOT_INVALID; /* Già colpita in precedenza */
    }

    if (current == CELL_EMPTY) {
        b->grid[row][col] = CELL_MISS;
        return SHOT_MISS;
    }

    if (current == CELL_SHIP) {
        b->grid[row][col] = CELL_HIT;
        int ship_idx = b->ship_map[row][col];
        if (out_ship_idx) *out_ship_idx = ship_idx;

        if (ship_idx >= 0 && ship_idx < NUM_SHIPS) {
            b->ships[ship_idx].hits++;
            if (b->ships[ship_idx].hits >= b->ships[ship_idx].size) {
                /* La nave è affondata! Aggiorniamo le sue celle a CELL_SUNK */
                b->ships[ship_idx].sunk = true;
                int sr = b->ships[ship_idx].row;
                int sc = b->ships[ship_idx].col;
                int len = b->ships[ship_idx].size;
                ShipDirection sdir = b->ships[ship_idx].dir;

                for (int i = 0; i < len; i++) {
                    int r = (sdir == DIR_VERTICAL) ? (sr + i) : sr;
                    int c = (sdir == DIR_HORIZONTAL) ? (sc + i) : sc;
                    if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                        b->grid[r][c] = CELL_SUNK;
                    }
                }

                b->ships_alive--;
                return SHOT_SUNK;
            }
        }

        return SHOT_HIT;
    }

    return SHOT_INVALID;
}

bool board_all_sunk(const Board *b) {
    if (!b) return true;
    return (b->ships_alive <= 0);
}
