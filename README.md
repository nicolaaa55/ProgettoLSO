# Progetto Laboratorio di Sistemi Operativi: Battaglia Navale (Client-Server in C)

**Corso di Laurea in Informatica - Esame da 6 CFU (Vecchio Ordinamento)**  
Sviluppato interamente in **C standard (C11 / POSIX.1-2008)**.

---

## 1. Descrizione del Progetto

Il progetto realizza un sistema **Client-Server concorrente** per giocare a **Battaglia Navale** in rete locale o remota.
Il server gestisce contemporaneamente molteplici client e partite parallele mediante **Thread POSIX (`pthread`)** e meccanismi di sincronizzazione basati su **Mutex**.

Tutte le regole di gioco, la validazione delle mosse, la gestione delle griglie e la determinazione degli esiti sono interamente centralizzate e validate **lato Server**, impedendo qualsiasi manomissione o incoerenza da parte dei client.

---

## 2. Requisiti e Funzionalità Implementate

- [x] **Server parallelo multi-client in C:** ogni client connesso viene gestito in un thread dedicato (`pthread_create`, `pthread_detach`).
- [x] **Comunicazione via Socket Stream (TCP):** nessun framework di alto livello o websocket; uso esclusivo di socket POSIX (`socket`, `bind`, `listen`, `accept`, `connect`).
- [x] **Lobby e Gestione Partite:**
  - Registrazione univoca del nickname.
  - Creazione di una nuova partita con identificativo univoco e nome personalizzato.
  - Elenco dinamico delle partite in attesa di avversario.
  - Richiesta di partecipazione e meccanismo di **accettazione o rifiuto** da parte dell'host.
- [x] **Fase 1 - Posizionamento Flotta (validata dal Server):**
  - Griglia 10x10 con flotta regolamentare di 5 navi (Portaerei [5], Corazzata [4], Incrociatore [3], Sottomarino [3], Cacciatorpediniere [2]).
  - Modalità di posizionamento **Manuale** (coordinate e orientamento cella per cella) oppure **Automatica** (generazione casuale valida rapida).
  - Controllo severo su limiti della scacchiera, orientamento e assenza di sovrapposizioni.
- [x] **Fase 2 - Combattimento Navale:**
  - Gestione rigorosa dei turni alternati.
  - Rilevazione dell'esito: *Acqua (MISS)*, *Colpito (HIT)*, *Colpito e Affondato (SUNK)* con notifica del nome della nave abbattuta.
  - Rilevamento della vittoria con messaggio personalizzato (*Vittoria* per il vincitore, *Sconfitta* per il perdente).
- [x] **Fase 3 - Nuova Sfida / Rivincita:**
  - Al termine del match, i due giocatori possono accordarsi per avviare una nuova partita mantenendo la stessa coppia o tornare alla lobby.
- [x] **Funzionalità Opzionale (pag. 3 traccia):**
  - Gestione della caduta/disconnessione di un giocatore: l'avversario rimasto viene immediatamente avvisato e può scegliere se:
    1. Concludere la partita ottenendo la **vittoria a tavolino** per abbandono.
    2. Rimanere nella stanza e **attendere l'ingresso di un nuovo sfidante**.
- [x] **Robustezza Estrema contro Input Malevoli (Fuzzing / Testing aggressivo):**
  - Nessun uso di funzioni vulnerabili come `scanf("%d")` o `gets()`.
  - Input gestito con buffer limitati, scarico automatico dei caratteri in eccesso dello `stdin` e parsing difensivo con `strtol`.
  - Server protetto contro buffer overflow su socket (`recv_line` con drenaggio automatico di righe giganti).
  - Gestione trasparente del segnale `SIGPIPE` (`MSG_NOSIGNAL` e `signal(SIGPIPE, SIG_IGN)`).
  - Pulizia e terminazione controllata su `SIGINT` (Ctrl+C).

---

## 3. Struttura della Repository

```
ProgettoLSO/
├── Makefile                # Script di build (make, make debug, make clean)
├── README.md               # Istruzioni d'uso e panoramica del progetto
├── doc/
│   └── DOCUMENTAZIONE.pdf  # Relazione tecnica sulle scelte implementative (max 10 pagine)
├── include/
│   ├── protocol.h          # Costanti, codici messaggio e definizioni condivise
│   ├── network.h           # Wrapper di rete robusti (send_all, recv_line, socket setup)
│   ├── board.h             # Gestione griglie, navi e risoluzione colpi
│   ├── game.h              # Struttura del match e sincronizzazione
│   ├── lobby.h             # Gestione concorrente di stanze e client
│   ├── ui.h                # Grafica a colori ANSI per terminale
│   └── utils.h             # Sanitizzazione input, conversioni e logger
└── src/
    ├── common/
    │   ├── network.c       # Primitive di comunicazione socket
    │   ├── board.c         # Logica navale pura e controlli di validità
    │   └── utils.c         # Parsing sicuro coordinate e numeri
    ├── server/
    │   ├── server.c        # Main server, gestione segnali e dispatcher thread
    │   ├── lobby.c         # Gestione thread-safe di client e stanze
    │   └── game.c          # Meccaniche di combattimento e turnazione
    └── client/
        ├── client.c        # Main client, loop utente e gestione risposte
        └── ui.c            # Rendering scacchiera a colori
```

---

## 4. Compilazione ed Esecuzione

### Requisiti di sistema
- Sistema operativo Linux / POSIX
- Compilatore `gcc` (compatibile C11)
- `make`
- Libreria `pthread` (inclusa nei sistemi Linux)

### Compilazione standard (Release)
```bash
make clean
make
```
Vengono generati i due eseguibili `server` e `client`.

### Compilazione per il Testing (Debug con AddressSanitizer)
Per verificare l'assenza di memory leak, corruzioni della memoria o undefined behavior durante i test:
```bash
make debug
```

---

## 5. Guida all'Utilizzo

### 1) Avvio del Server
In un terminale dedicato, avviare il server specificando facoltativamente la porta (default: `8888`):
```bash
./server 8888
```
Il server si porrà in ascolto, stampando log dettagliati su connessioni, eventi di lobby e mosse di gioco.

### 2) Avvio del Primo Giocatore (Host)
In un secondo terminale:
```bash
./client 127.0.0.1 8888
```
1. Inserire il proprio nickname (es. `Alice`).
2. Dal menu principale, selezionare `1) Crea una nuova partita`.
3. Inserire un nome per la stanza (es. `StanzaDiAlice`).
4. Il client si mette in attesa di uno sfidante.

### 3) Avvio del Secondo Giocatore (Sfidante)
In un terzo terminale:
```bash
./client 127.0.0.1 8888
```
1. Inserire il proprio nickname (es. `Bob`).
2. Selezionare `2) Visualizza le partite aperte ed unisciti`.
3. Verrà mostrata la tabella delle stanze disponibili: digitare l'ID della partita di Alice.
4. Sul terminale di Alice comparirà la notifica:
   `Lo sfidante 'Bob' desidera unirsi alla tua partita! Accetti la sfida? (1=Si, 0=No)`
5. Se Alice accetta (`1`), la partita si avvia immediatamente per entrambi.

### 4) Posizionamento della Flotta
Ogni giocatore può scegliere se:
- **Posizionare manualmente le 5 navi**, specificando per ciascuna la coordinata di partenza (es. `B4`, `A1`, `J10`) e l'orientamento (`H` orizzontale, `V` verticale). Il sistema verifica sia in locale che sul server la validità e impedisce sovrapposizioni o uscite dalla griglia.
- **Posizionare automaticamente la flotta**, ideale per test rapidi o per la verifica delle funzionalità di combattimento.

### 5) Combattimento a Turni
Una volta completato il posizionamento:
- Il terminale visualizza a colori:
  - A sinistra: **La propria flotta**, con lo stato delle proprie navi e i colpi subiti dal nemico.
  - A destra: **Il radar nemico**, dove compaiono i propri colpi dichiarati (`O` per acqua, `X` per nave colpita, `*` per affondata).
- I giocatori si alternano dichiarando le coordinate (es. `C5`, `F8`).
- Il gioco termina non appena tutte le 5 navi di un partecipante vengono affondate.

### 6) Rivincita o Uscita
A fine partita, entrambi i giocatori possono votare se avviare una **rivincita immediata** (stessa stanza e stessi giocatori) oppure tornare alla lobby.
In caso di caduta/disconnessione di uno dei due, l'altro giocatore ha facoltà di reclamare la vittoria a tavolino o rimettere la stanza in attesa di un nuovo utente.
