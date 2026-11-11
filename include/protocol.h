#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_PORT 8888
#define DEFAULT_HOST "127.0.0.1"

#define BUFFER_SIZE 512
#define MAX_LINE_LEN 512
#define MAX_NICK_LEN 32
#define MAX_GAME_NAME_LEN 32

#define BOARD_SIZE 10
#define NUM_SHIPS 5

/* Tipi di navi e dimensioni standard */
typedef struct {
    const char *name;
    int size;
} ShipSpec;

static const ShipSpec SHIP_SPECS[NUM_SHIPS] = {
    {"Portaerei", 5},
    {"Corazzata", 4},
    {"Incrociatore", 3},
    {"Sottomarino", 3},
    {"Cacciatorpediniere", 2}
};

/* Stati di una cella */
typedef enum {
    CELL_EMPTY = 0,     /* Mare aperto vuoto '~' */
    CELL_SHIP,          /* Presenza di una nave 'S' */
    CELL_MISS,          /* Colpo a vuoto 'O' */
    CELL_HIT,           /* Nave colpita 'X' */
    CELL_SUNK           /* Nave colpita e affondata '*' */
} CellState;

/* Orientamento nave */
typedef enum {
    DIR_HORIZONTAL = 0, /* Orizzontale verso destra (default H o D) */
    DIR_RIGHT = 0,
    DIR_VERTICAL = 1,   /* Verticale verso il basso (default V o B) */
    DIR_DOWN = 1,
    DIR_LEFT = 2,       /* Orizzontale verso sinistra (S) */
    DIR_UP = 3          /* Verticale verso l'alto (A) */
} ShipDirection;

/* Stato del client nel protocollo */
typedef enum {
    STATE_CONNECTED = 0,       /* Connesso, deve impostare o ha impostato il nickname */
    STATE_LOBBY,               /* Nella lobby: crea partita, unisciti, lista partite */
    STATE_WAITING_OPPONENT,    /* Creatore della partita in attesa di avversario */
    STATE_CHALLENGE_PENDING,   /* Creatore ha ricevuto richiesta, deve accettare/rifiutare */
    STATE_WAITING_ACCEPT,      /* Sfidante in attesa di accettazione da parte del creatore */
    STATE_PLACING_SHIPS,       /* Posizionamento navi in corso */
    STATE_PLACING_WAITING,     /* Navi posizionate, in attesa che l'avversario finisca */
    STATE_IN_GAME_MY_TURN,     /* In partita: tocca a questo giocatore sparare */
    STATE_IN_GAME_WAIT_TURN,   /* In partita: tocca all'avversario sparare */
    STATE_GAME_OVER,           /* Partita conclusa: scelta rivincita o uscita */
    STATE_OPPONENT_LEFT        /* L'avversario si è disconnesso */
} ClientState;

/* Risultato di un colpo */
typedef enum {
    SHOT_INVALID = 0,
    SHOT_MISS,
    SHOT_HIT,
    SHOT_SUNK
} ShotResult;

/* Costanti di limite per lobby */
#define MAX_GAMES 32
#define MAX_CLIENTS 64

#endif /* PROTOCOL_H */
