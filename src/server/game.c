#include "game.h"
#include "lobby.h"
#include "utils.h"
#include <string.h>

void game_init(Game *g, int id, const char *name, ClientSession *host) {
    if (!g) return;
    pthread_mutex_init(&g->lock, NULL);

    pthread_mutex_lock(&g->lock);
    g->id = id;
    safe_str_copy(g->name, name, sizeof(g->name));
    g->host = host;
    g->guest = NULL;
    g->pending_challenger = NULL;

    board_init(&g->host_board);
    board_init(&g->guest_board);

    g->in_use = true;
    g->host_placement_done = false;
    g->guest_placement_done = false;
    g->current_turn = 1; /* Host inizia per convenzione */
    g->game_over = false;
    g->winner = 0;
    g->host_rematch = -1;
    g->guest_rematch = -1;
    pthread_mutex_unlock(&g->lock);
}

void game_reset(Game *g) {
    if (!g) return;
    pthread_mutex_lock(&g->lock);
    board_reset(&g->host_board);
    board_reset(&g->guest_board);
    g->host_placement_done = false;
    g->guest_placement_done = false;
    g->current_turn = 1;
    g->game_over = false;
    g->winner = 0;
    g->host_rematch = -1;
    g->guest_rematch = -1;
    pthread_mutex_unlock(&g->lock);
}

bool game_place_ship(Game *g, ClientSession *player, int ship_idx, int row, int col, ShipDirection dir, const char **err_msg) {
    if (!g || !player) {
        if (err_msg) *err_msg = "Parametri non validi.";
        return false;
    }

    pthread_mutex_lock(&g->lock);

    Board *b = NULL;
    if (player == g->host) {
        b = &g->host_board;
    } else if (player == g->guest) {
        b = &g->guest_board;
    } else {
        pthread_mutex_unlock(&g->lock);
        if (err_msg) *err_msg = "Giocatore non appartenente a questa partita.";
        return false;
    }

    if (!board_can_place_ship(b, ship_idx, row, col, dir, err_msg)) {
        pthread_mutex_unlock(&g->lock);
        return false;
    }

    board_place_ship(b, ship_idx, row, col, dir);

    if (player == g->host && b->ships_placed == NUM_SHIPS) {
        g->host_placement_done = true;
    } else if (player == g->guest && b->ships_placed == NUM_SHIPS) {
        g->guest_placement_done = true;
    }

    pthread_mutex_unlock(&g->lock);
    return true;
}

bool game_auto_place(Game *g, ClientSession *player) {
    if (!g || !player) return false;

    pthread_mutex_lock(&g->lock);

    Board *b = NULL;
    if (player == g->host) {
        b = &g->host_board;
        board_reset(b);
        board_auto_place_all(b);
        g->host_placement_done = true;
    } else if (player == g->guest) {
        b = &g->guest_board;
        board_reset(b);
        board_auto_place_all(b);
        g->guest_placement_done = true;
    } else {
        pthread_mutex_unlock(&g->lock);
        return false;
    }

    pthread_mutex_unlock(&g->lock);
    return true;
}

bool game_handle_shot(Game *g, ClientSession *shooter, int row, int col,
                      ShotResult *out_res, int *out_ship_idx,
                      int *out_sunk_row, int *out_sunk_col, int *out_sunk_len, ShipDirection *out_sunk_dir,
                      bool *out_game_over, const char **err_msg) {
    if (!g || !shooter || !out_res || !out_game_over) {
        if (err_msg) *err_msg = "Parametri non validi.";
        return false;
    }

    if (out_sunk_row) *out_sunk_row = -1;
    if (out_sunk_col) *out_sunk_col = -1;
    if (out_sunk_len) *out_sunk_len = 0;
    if (out_sunk_dir) *out_sunk_dir = DIR_HORIZONTAL;

    pthread_mutex_lock(&g->lock);

    if (g->game_over) {
        pthread_mutex_unlock(&g->lock);
        if (err_msg) *err_msg = "La partita è già terminata.";
        return false;
    }

    bool is_host = (shooter == g->host);
    bool is_guest = (shooter == g->guest);

    if (!is_host && !is_guest) {
        pthread_mutex_unlock(&g->lock);
        if (err_msg) *err_msg = "Non fai parte di questa partita.";
        return false;
    }

    if ((is_host && g->current_turn != 1) || (is_guest && g->current_turn != 2)) {
        pthread_mutex_unlock(&g->lock);
        if (err_msg) *err_msg = "Non è il tuo turno di gioco!";
        return false;
    }

    Board *target_board = is_host ? &g->guest_board : &g->host_board;
    int hit_ship = -1;
    ShotResult res = board_receive_shot(target_board, row, col, &hit_ship);

    if (res == SHOT_INVALID) {
        pthread_mutex_unlock(&g->lock);
        if (err_msg) *err_msg = "Cella non valida o già colpita in precedenza.";
        return false;
    }

    *out_res = res;
    if (out_ship_idx) *out_ship_idx = hit_ship;

    if (res == SHOT_SUNK && hit_ship >= 0 && hit_ship < NUM_SHIPS) {
        if (out_sunk_row) *out_sunk_row = target_board->ships[hit_ship].row;
        if (out_sunk_col) *out_sunk_col = target_board->ships[hit_ship].col;
        if (out_sunk_len) *out_sunk_len = target_board->ships[hit_ship].size;
        if (out_sunk_dir) *out_sunk_dir = target_board->ships[hit_ship].dir;
    }

    if (board_all_sunk(target_board)) {
        g->game_over = true;
        g->winner = is_host ? 1 : 2;
        *out_game_over = true;
    } else {
        *out_game_over = false;
        /* Il turno passa all'altro giocatore */
        g->current_turn = (g->current_turn == 1) ? 2 : 1;
    }

    pthread_mutex_unlock(&g->lock);
    return true;
}
