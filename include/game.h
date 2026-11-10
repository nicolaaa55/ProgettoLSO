#ifndef GAME_H
#define GAME_H

#include "protocol.h"
#include "board.h"
#include <pthread.h>

struct ClientSession;

typedef struct Game {
    int id;
    char name[MAX_GAME_NAME_LEN];
    struct ClientSession *host;
    struct ClientSession *guest;
    struct ClientSession *pending_challenger;

    Board host_board;
    Board guest_board;

    bool in_use;
    bool host_placement_done;
    bool guest_placement_done;

    int current_turn; /* 1 = Host, 2 = Guest */
    bool game_over;
    int winner;       /* 1 = Host, 2 = Guest */

    int host_rematch;  /* -1 = undecided, 0 = no, 1 = yes */
    int guest_rematch; /* -1 = undecided, 0 = no, 1 = yes */

    pthread_mutex_t lock;
} Game;

void game_init(Game *g, int id, const char *name, struct ClientSession *host);
void game_reset(Game *g);

/* Risoluzione azioni di gioco */
bool game_place_ship(Game *g, struct ClientSession *player, int ship_idx, int row, int col, ShipDirection dir, const char **err_msg);
bool game_auto_place(Game *g, struct ClientSession *player);
bool game_handle_shot(Game *g, struct ClientSession *shooter, int row, int col, ShotResult *out_res, int *out_ship_idx, bool *out_game_over, const char **err_msg);

#endif /* GAME_H */
