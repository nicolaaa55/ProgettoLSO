# Relazione Tecnica e Scelte Implementative: Battaglia Navale Client-Server

**Insegnamento:** Laboratorio di Sistemi Operativi (Esame da 6 CFU)  
**Corso di Laurea:** Laurea Triennale in Informatica, Università degli Studi di Napoli Federico II  
**Linguaggio di Programmazione:** C standard (C11 / Standard POSIX.1-2008)  
**Traccia:** Progetto 2 - Battaglia Navale Concorrente  

**Componenti del Gruppo:**
- **Carrino Emanuele** — Matricola: **N86005084**
- **Ascione Nicola** — Matricola: **N86005133**

---

## 1. Introduzione ed Obiettivi

La presente relazione descrive l'architettura software, le decisioni progettuali e le soluzioni algoritmiche adottate per lo sviluppo del progetto di **Battaglia Navale Client-Server**, conforme ai requisiti dell'esame da **6 CFU**.

In conformità alle specifiche di progetto:
- L'intero sistema è implementato in linguaggio **C standard (C11 / POSIX.1-2008)** sia per la componente Server che per la componente Client, senza ricorrere a container Docker.
- La comunicazione tra processi remoti o locali avviene esclusivamente tramite **Socket Stream (TCP)**.
- Il Server gestisce i client in parallelo mediante **Thread POSIX (`pthread`)**.
- Tutte le regole di gioco, la validazione delle mosse e la gestione dello stato sono interamente centralizzate nel Server (**Server Autorevole**), impedendo qualsiasi manomissione o incoerenza da parte dei client.
- Il codice è stato progettato con criteri di **sicurezza e robustezza difensiva**: nessun uso di funzioni vulnerabili a loop infiniti o overflow (`scanf("%d")`, `gets()`), ma gestione controllata di `stdin` e dei canali socket.

---

## 2. Architettura di Rete e Comunicazione Socket TCP

### 2.1 Dominio e Stile di Comunicazione
La comunicazione distribuita adotta l'interfaccia delle **Socket POSIX**:
- **Dominio:** `AF_INET` (Internet IPv4, indirizzi IP a 32 bit).
- **Stile di comunicazione:** `SOCK_STREAM`, canale di trasmissione bidirezionale a flusso, sequenziale, affidabile e orientato alla connessione (protocollo TCP).
- **Protocollo:** `0` (protocollo di trasporto IP predefinito, TCP).

Rispetto a un canale a datagrammi (UDP), la scelta di TCP garantisce l'assenza di perdita o duplicazione di pacchetti, caratteristica indispensabile per la corretta sequenzialità dei comandi in un gioco a turni.

### 2.2 Configurazione degli Indirizzi (`struct sockaddr_in` e `inet_aton`)
1. **Configurazione del Server:**  
   Il server alloca una struttura `struct sockaddr_in` e la configura per l'ascolto:
   - `sin_family = AF_INET;`
   - `sin_port = htons(PORT);` (conversione del numero di porta in network byte order tramite `htons`);
   - `sin_addr.s_addr = htonl(INADDR_ANY);` (ascolto su tutte le interfacce disponibili);
   - Azzeramento del campo di padding `sin_zero` tramite `memset()`.
   Per evitare l'errore di porta già occupata (*Address already in use*) in caso di riavvio rapido del server, viene abilitata l'opzione `SO_REUSEADDR` tramite `setsockopt()`. Segue quindi l'associazione dell'indirizzo con `bind()` e l'attivazione della coda di ascolto con `listen()`.

2. **Configurazione del Client:**  
   Il client specifica l'indirizzo IP del server convertendo la stringa testuale dotted (es. `"127.0.0.1"`) nella rappresentazione binaria di rete tramite la funzione standard POSIX `inet_aton()`. La connessione viene quindi instaurata mediante la system call `connect()`.

In caso di fallimento di una qualsiasi chiamata di sistema, il descrittore viene chiuso e l'errore viene tempestivamente diagnosticato tramite `perror()` o `strerror()`.

---

## 3. Modello di Concorrenza: Server Multithreaded

### 3.1 Scelta Architetturale: Thread POSIX vs Processi Figli (fork)
Le specifiche di progetto consentivano la gestione concorrente dei client tramite thread oppure processi figli generati con `fork()`.  
Per l'implementazione del server si è scelto il modello **Thread-per-Client** basato sulla libreria POSIX Threads (`pthread`), per le seguenti ragioni:

1. **Condivisione della Memoria Efficiente e Naturale:**  
   In un gioco strategico a due partecipanti, i thread associati ai due giocatori devono interagire continuamente sulle medesime strutture dati (la stanza condivisa, le scacchiere dei giocatori, la notifica dei turni). L'uso di processi distinti generati con `fork()` avrebbe richiesto aree di memoria condivisa POSIX (`shm_open`, `mmap`) e semafori di sistema, aumentando sensibilmente la complessità architetturale e il rischio di risorse orfane in caso di crash. I thread condividono invece lo spazio d'indirizzamento del processo padre in modo trasparente.
2. **Minore Overhead:**  
   La creazione e commutazione di contesto di un thread (`pthread_create`) è notevolmente più leggera e performante rispetto alla duplicazione dell'intero spazio di memoria di un processo mediante `fork()`.

### 3.2 Ciclo di Vita del Thread e `pthread_detach()`
Il processo principale del server esegue un ciclo continuo in cui rimane in attesa su `accept()`. All'arrivo di una nuova connessione:
1. `accept()` restituisce il descrittore socket dedicato al nuovo client.
2. Viene registrata la sessione nella tabella dei client attivi.
3. Viene avviato un thread worker dedicato invocando `pthread_create()`.

All'interno della funzione worker, la prima istruzione eseguita è:
```c
pthread_detach(pthread_self());
```
Questo garantisce che, quando il client si disconnette o termina la sessione, il thread rilasci automaticamente tutte le risorse allocate (stack e descrittori) al kernel, senza richiedere chiamate bloccanti a `pthread_join()` nel thread principale e prevenendo l'accumulo di thread in stato zombie.

---

## 4. Sincronizzazione tra Thread e Prevenzione dei Deadlock

Poiché molteplici thread worker accedono concorrentemente alle strutture condivise della lobby e delle partite, è indispensabile garantire la mutua esclusione per prevenire **Race Conditions**.

### 4.1 Granularità dei Mutex
Sono stati definiti tre livelli di lock con granularità differenziata:

1. **`g_lobby_mutex` (Mutex Statico con `PTHREAD_MUTEX_INITIALIZER`):**  
   Protegge l'array globale dei client registrati e la tabella delle stanze disponibili. Viene acquisito per creare una nuova partita, richiedere l'elenco delle partite o disconnettere un client.
2. **`Game.lock` (Mutex Dinamico per Partita con `pthread_mutex_init`):**  
   Ciascuna partita possiede un proprio mutex allocato nella relativa struttura. Questa granularità fine consente a più partite indipendenti di svolgersi in parallelo senza interferire l'una con l'altra.
3. **`ClientSession.send_lock`:**  
   Garantisce la mutua esclusione nelle scritture verso la socket di ciascun client. Poiché un thread (es. il Giocatore 1) può avere la necessità di inviare una notifica asincrona all'avversario (Giocatore 2), il lock `send_lock` garantisce che le invocazioni di `send_line()` siano sempre atomiche e che i flussi di byte non si sovrappongano.

### 4.2 Risoluzione Formale del Deadlock
Per evitare situazioni di stallo (*Deadlock*), è stata adottata una regola di **acquisizione gerarchica unidirezionale**:
- Qualora un'operazione richieda sia il lock della lobby che il lock di una partita, **viene sempre acquisito per primo `g_lobby_mutex` e successivamente `Game.lock`**.
- Il rilascio dei lock avviene nell'ordine inverso.
- Non esiste alcun percorso di esecuzione in cui un thread tenta di acquisire `g_lobby_mutex` mantenendo attivo il lock di una partita. In tal modo viene matematicamente eliminata la possibilità di dipendenze circolari tra risorse.

---

## 5. Multiplazione dell'I/O nel Client: System Call `select()`

Nel Client si presenta un tipico problema di concorrenza nell'input/output:
- Quando un utente crea una partita e attende uno sfidante, il client deve poter ricevere la notifica di sfida dal server (`CHALLENGE_REQUEST`), ma deve anche consentire all'utente di digitare un comando da tastiera (ad esempio per annullare la stanza digitando `cancel` o `0`).
- Se il processo si bloccasse su una chiamata di lettura ordinaria su `stdin` (come `fgets`), non potrebbe ricevere i messaggi provenienti dalla rete fino alla pressione di un tasto.

### 5.1 Implementazione con `select()`
Per monitorare contemporaneamente più descrittori senza generare attese bloccanti, è stata impiegata la system call POSIX **`select()`**:
```c
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(STDIN_FILENO, &readfds);
FD_SET(sock, &readfds);
int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);

if (FD_ISSET(STDIN_FILENO, &readfds)) {
    /* Dati disponibili dallo standard input (tastiera) */
}
if (FD_ISSET(sock, &readfds)) {
    /* Messaggio ricevuto dal Server tramite il socket */
}
```
Questa soluzione consente di gestire la multiplazione dell'I/O in modo efficiente e leggero, evitando la complessità di dover generare thread secondari nel client.

---

## 6. Trattamento dei Segnali Asincroni (`SIGPIPE` e `SIGINT`)

### 6.1 Neutralizzazione del Segnale `SIGPIPE`
Nelle comunicazioni via socket TCP, se un processo tenta di scrivere su un descrittore il cui estremo remoto è stato chiuso improvvisamente (ad esempio per arresto forzato del client o perdita di connettività), il kernel genera il segnale `SIGPIPE`. L'azione predefinita di `SIGPIPE` in ambiente UNIX è la terminazione immediata del processo.

Per impedire che la caduta di un singolo client causi il crash dell'intero server:
1. All'avvio del server viene disattivata l'azione predefinita con: `signal(SIGPIPE, SIG_IGN);`.
2. Nelle chiamate di invio su rete viene impostato il flag `MSG_NOSIGNAL` in `send()`.
In questo modo, la scrittura fallita restituisce semplicemente `-1` con `errno == EPIPE`, consentendo al thread worker di intercettare l'errore e procedere alla chiusura ordinata della sessione.

### 6.2 Gestione Controllata di `SIGINT` (Ctrl+C)
Sia nel server che nel client viene registrata una funzione di gestione per `SIGINT` tramite `sigaction()`. Quando l'utente preme Ctrl+C, il signal handler imposta una variabile di terminazione e chiude le socket aperte, consentendo al programma di deallocare ordinatamente le strutture in memoria e uscire in modo controllato.

---

## 7. Robustezza e Gestione Difensiva dell'Input (Hardening Anti-Break)

Il progetto è stato sviluppato con un'attenzione rigorosa alla prevenzione di anomalie e vulnerabilità tipiche dei programmi scritti in linguaggio C:

### 7.1 Gestione Sicura dell'Input Utente (`safe_read_line`)
- **Vulnerabilità di `scanf("%d")` ed eliminazione di `gets()`:** se l'utente digita caratteri non numerici (es. `abc`) in risposta a una richiesta di numero con `scanf("%d")`, la funzione fallisce senza estrarre i caratteri dallo stream `stdin`, provocando un loop infinito di letture fallite.
- **Funzione `safe_read_line()`:** acquisisce l'input mediante `fgets()` su un buffer di dimensione prefissata. Qualora la stringa inserita superi la capienza del buffer, un ciclo iterativo su `getchar()` **drena e scarta tutti i caratteri eccedenti dallo stream standard input** fino al newline `\n`.
- **Conversione numerica rigida con `strtol()`:** la funzione `parse_int_in_range()` verifica che l'intera stringa sia composta esclusivamente da cifre numeriche valide (analizzando il puntatore `endptr` ed `errno`). Se l'utente inserisce lettere, numeri negativi o valori fuori intervallo, il dato viene respinto con un messaggio d'errore e richiesto nuovamente, senza alcun blocco del programma.

### 7.2 Protezione del Server da Fuzzing e Buffer Overflow (`recv_line`)
Sul canale di rete, un utente malevolo o uno script automatizzato potrebbe trasmettere righe prive di newline lunghe migliaia di byte.  
La funzione `recv_line()` legge i byte dallo stream TCP fino al delimitatore `\n`. Qualora la riga superi la capienza del buffer, i byte in eccesso vengono letti e scartati fino al carattere newline. In questo modo:
- Nessun buffer interno subisce overflow.
- Il comando malformato viene scartato.
- Lo stream TCP viene mantenuto sincronizzato e pulito per il comando successivo.

### 7.3 Macchina a Stati Finiti Zero-Trust
Il server adotta una logica "Zero-Trust": ciascun comando ricevuto viene validato rispetto allo stato corrente del client (`ClientState`). Se un client invia un comando fuori contesto (ad esempio tenta di sparare mentre si trova nella lobby o durante il turno dell'avversario), il comando viene rifiutato con `ERR_INVALID_STATE` o `ERR_NOT_YOUR_TURN`, senza alterare la partita.

---

## 8. Logica di Gioco e Strutture Dati

La logica del gioco navale è incapsulata nel modulo `board.c`:
- **Griglia:** matrice 10x10 rappresentata da tipi enumerati `CellState` (`CELL_EMPTY`, `CELL_SHIP`, `CELL_MISS`, `CELL_HIT`, `CELL_SUNK`).
- **Flotta Regolamentare (5 navi):**
  1. Portaerei (5 caselle)
  2. Corazzata (4 caselle)
  3. Incrociatore (3 caselle)
  4. Sottomarino (3 caselle)
  5. Cacciatorpediniere (2 caselle)
- **Validazione del Posizionamento:** la funzione `board_can_place_ship()` verifica che le coordinate siano interne alla griglia `[0..9]`, che la nave non superi i bordi e che non vi siano sovrapposizioni con navi già presenti. Il sistema supporta l'estensione della nave in tutte e quattro le direzioni cardinali a partire dalla coordinata indicata: verso **Destra** o **Sinistra** per l'orientamento orizzontale, e verso il **Basso** o l'**Alto** per quello verticale. Se entrambe le direzioni sono fisicamente ammissibili dalla cella selezionata, il client offre all'utente la scelta; se solo una direzione è praticabile, essa viene applicata automaticamente.
- **Risoluzione dei Colpi:** la funzione `board_receive_shot()` determina l'esito (*Acqua*, *Colpito*, *Affondato*). Quando tutte le celle di una nave sono state colpite, la nave viene marcata come affondata (`CELL_SUNK`) e viene decrementato il contatore delle navi vive. Il raggiungimento di `ships_alive == 0` decreta la vittoria.

---

## 9. Gestione dei Casi Limite e Requisiti Opzionali

### 9.1 Disconnessione Improvvisa dell'Avversario (Requisito Opzionale pag. 3)
In accordo con la specifica opzionale della traccia (pagina 3):
- Se un giocatore cade durante la partita, il server rileva la chiusura della socket e notifica l'avversario tramite il messaggio `OPPONENT_DISCONNECTED`.
- Al giocatore superstite viene presentata una scelta:
  1. **Terminare la partita con vittoria a tavolino:** la sessione viene conclusa con vittoria per forfait e il giocatore torna alla lobby principale; in questo scenario la richiesta di rivincita viene naturalmente inibita dal sistema, non essendo più presente l'avversario.
  2. **Attendere un nuovo giocatore nella stanza:** la stanza viene reimpostata in stato di attesa (`STATE_WAITING_OPPONENT`), consentendo a un altro utente della lobby di unirsi.

### 9.2 Meccanismo di Rivincita (Rematch)
Al termine del match, entrambi i partecipanti possono scegliere se disputare una nuova sfida:
- Se entrambi votano per la rivincita (`REMATCH 1`), il server azzera le scacchiere e riavvia la fase di posizionamento con la stessa coppia di giocatori.
- Se uno dei due rifiuta (`REMATCH 0`), entrambi tornano alla lobby principale e la stanza viene liberata.

---

## 10. Conclusioni e Risultati dei Test Sperimentali

Il sistema è stato sottoposto a test rigorosi sia in modalità manuale interattiva che tramite script automatizzati di stress-testing e fuzzing. I collaudi hanno confermato:
- **Compilazione pulita:** zero warning con `gcc -Wall -Wextra -Wpedantic -std=c11`.
- **Integrità della memoria:** collaudo superato con `AddressSanitizer` (assenza di memory leak, corruzioni o crash).
- **Resilienza totale:** gestione deterministica di input malformati e disconnessioni repentine.

Il progetto soddisfa integralmente tutte le specifiche funzionali e architetturali richieste per l'esame di Laboratorio di Sistemi Operativi.
