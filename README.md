# Laboratorio di Sistemi Operativi: Battaglia Navale Client-Server (C11 / POSIX)

**Candidati:**
- Carrino Emanuele (Matricola: N86005084)
- Ascione Nicola (Matricola: N86005133)

**Corso di Laurea Triennale in Informatica — Università degli Studi di Napoli Federico II**  
**Insegnamento:** Laboratorio di Sistemi Operativi (Esame da 6 CFU)

---

## 1. Requisiti di Sistema

- **Sistema Operativo:** Linux / ambiente POSIX compatibile.
- **Compilatore:** `gcc` con supporto a C11 e POSIX.1-2008.
- **Build Tool:** GNU `make`.
- **Librerie:** `pthread` (inclusa nella libreria standard C su Linux).

---

## 2. Istruzioni per la Compilazione

Dalla radice del progetto, utilizzare il `Makefile` fornito:

### Compilazione Standard (Release)
Compila sia il server che il client con ottimizzazioni `-O2` e tutti i controlli di correttezza attivi (`-Wall -Wextra -Wpedantic`):
```bash
make clean
make
```
Al termine vengono generati i due eseguibili binari:
- `server`
- `client`

### Compilazione in Modalità Debug (Sanitizer)
Compila con simboli di debug `-g` e controlli dinamici per memoria e comportamenti indefiniti (`-fsanitize=address,undefined`):
```bash
make debug
```

### Pulizia dei File Compilati
Rimuove gli eseguibili e i file oggetto generati:
```bash
make clean
```

---

## 3. Istruzioni per l'Esecuzione

### 3.1 Avvio del Server

Sintassi:
```bash
./server [porta]
```

- **`porta`** *(opzionale)*: numero della porta TCP su cui il server si pone in ascolto.  
  Se omesso, viene utilizzata la porta predefinita `8888`.

Esempio:
```bash
./server 8888
```

---

### 3.2 Avvio del Client

Sintassi:
```bash
./client [indirizzo_ip] [porta]
```

- **`indirizzo_ip`** *(opzionale)*: indirizzo IPv4 del server a cui connettersi.  
  Se omesso, viene utilizzato `127.0.0.1` (localhost).
- **`porta`** *(opzionale)*: porta TCP del server a cui connettersi.  
  Se omesso, viene utilizzata la porta predefinita `8888`.

#### Esempi di Esecuzione:

1. **Esecuzione in locale (stesso computer):**
   - Nel primo terminale (Server):
     ```bash
     ./server 8888
     ```
   - Nel secondo terminale (Client 1):
     ```bash
     ./client 127.0.0.1 8888
     ```
   - Nel terzo terminale (Client 2):
     ```bash
     ./client 127.0.0.1 8888
     ```

2. **Esecuzione in rete (due computer diversi nella stessa rete o tramite IP raggiungibile):**
   - Sul computer del Server (supponendo che il suo indirizzo IP sia `192.168.1.100`):
     ```bash
     ./server 8888
     ```
   - Sul computer del Client 1:
     ```bash
     ./client 192.168.1.100 8888
     ```
   - Sul computer del Client 2:
     ```bash
     ./client 192.168.1.100 8888
     ```

---

## 4. Documentazione Tecnica

La trattazione dettagliata dell'architettura del sistema, delle decisioni progettuali e delle scelte implementative è contenuta nella relazione tecnica in formato PDF:
- `doc/DOCUMENTAZIONE.pdf`
