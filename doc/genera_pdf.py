#!/usr/bin/env python3
import sys
import os
from reportlab.lib.pagesizes import A4
from reportlab.lib import colors
from reportlab.lib.units import cm
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.pdfgen import canvas

class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_decorations(num_pages)
            super().showPage()
        super().save()

    def draw_page_decorations(self, page_count):
        if self._pageNumber == 1:
            return  # Nessun header o footer sulla copertina

        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#555555"))

        # Header superiore
        self.drawString(2.0 * cm, 28.3 * cm, "Laboratorio di Sistemi Operativi - Battaglia Navale Client-Server (C11 / POSIX)")
        self.setStrokeColor(colors.HexColor("#CCCCCC"))
        self.setLineWidth(0.5)
        self.line(2.0 * cm, 28.1 * cm, 19.0 * cm, 28.1 * cm)

        # Footer inferiore
        self.line(2.0 * cm, 1.8 * cm, 19.0 * cm, 1.8 * cm)
        self.drawString(2.0 * cm, 1.4 * cm, "Università degli Studi di Napoli Federico II - Corso di Laurea in Informatica")
        page_str = f"Pagina {self._pageNumber} di {page_count}"
        self.drawRightString(19.0 * cm, 1.4 * cm, page_str)
        self.restoreState()


def build_pdf(filename):
    doc = SimpleDocTemplate(
        filename,
        pagesize=A4,
        leftMargin=2.0 * cm,
        rightMargin=2.0 * cm,
        topMargin=2.2 * cm,
        bottomMargin=2.2 * cm
    )

    styles = getSampleStyleSheet()

    PRIMARY = colors.HexColor("#002B49")    # Blu Istituzionale
    SECONDARY = colors.HexColor("#005A9C")  # Blu secondario
    TEXT_DARK = colors.HexColor("#222222")  # Testo scuro
    ACCENT_BG = colors.HexColor("#F4F7F9")  # Sfondo box

    title_cover = ParagraphStyle(
        'CoverTitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=24,
        leading=30,
        alignment=1,
        textColor=PRIMARY
    )

    subtitle_cover = ParagraphStyle(
        'CoverSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=13,
        leading=18,
        alignment=1,
        textColor=SECONDARY
    )

    univ_cover = ParagraphStyle(
        'CoverUniv',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=15,
        leading=20,
        alignment=1,
        textColor=PRIMARY
    )

    dept_cover = ParagraphStyle(
        'CoverDept',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=11,
        leading=15,
        alignment=1,
        textColor=colors.HexColor("#555555")
    )

    h1_style = ParagraphStyle(
        'Heading1_Custom',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=13.5,
        leading=17.5,
        spaceBefore=14,
        spaceAfter=6,
        textColor=PRIMARY,
        keepWithNext=True
    )

    h2_style = ParagraphStyle(
        'Heading2_Custom',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=11,
        leading=15,
        spaceBefore=10,
        spaceAfter=4,
        textColor=SECONDARY,
        keepWithNext=True
    )

    body_style = ParagraphStyle(
        'Body_Custom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=9.5,
        leading=13.5,
        spaceBefore=3,
        spaceAfter=5,
        textColor=TEXT_DARK,
        alignment=4  # Justified
    )

    bullet_style = ParagraphStyle(
        'Bullet_Custom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=9.5,
        leading=13.5,
        leftIndent=15,
        spaceBefore=2,
        spaceAfter=2,
        textColor=TEXT_DARK
    )

    box_text = ParagraphStyle(
        'BoxText',
        parent=styles['Normal'],
        fontName='Helvetica-Oblique',
        fontSize=9,
        leading=13,
        textColor=colors.HexColor("#1A252F")
    )

    code_inline = ParagraphStyle(
        'CodeStyle',
        parent=styles['Normal'],
        fontName='Courier',
        fontSize=8.5,
        leading=11,
        textColor=colors.HexColor("#8A1010")
    )

    story = []

    # ==================== COPERTINA ====================
    story.append(Spacer(1, 2.0 * cm))
    story.append(Paragraph("UNIVERSITÀ DEGLI STUDI DI NAPOLI FEDERICO II", univ_cover))
    story.append(Spacer(1, 0.3 * cm))
    story.append(Paragraph("Scuola Politecnica e delle Scienze di Base<br/>Corso di Laurea Triennale in Informatica", dept_cover))
    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("<b>Insegnamento:</b> Laboratorio di Sistemi Operativi (Esame da 6 CFU)", dept_cover))
    story.append(Spacer(1, 0.8 * cm))
    story.append(HRFlowable(width="80%", thickness=2, color=PRIMARY, spaceBefore=5, spaceAfter=20))

    story.append(Spacer(1, 1.2 * cm))
    story.append(Paragraph("RELAZIONE TECNICA E SCELTE IMPLEMENTATIVE", title_cover))
    story.append(Spacer(1, 0.5 * cm))
    story.append(Paragraph("<b>Progetto 2: Battaglia Navale Concorrente</b><br/>Architettura Client-Server in C con Socket TCP e Thread POSIX", subtitle_cover))
    story.append(Spacer(1, 1.5 * cm))

    # Tabella Candidati
    candidati_data = [
        [
            Paragraph("<b>Componenti del Gruppo:</b>", ParagraphStyle('C1', fontName='Helvetica-Bold', fontSize=10, textColor=PRIMARY)),
            Paragraph("<b>Matricola:</b>", ParagraphStyle('C2', fontName='Helvetica-Bold', fontSize=10, textColor=PRIMARY))
        ],
        [
            Paragraph("Carrino Emanuele", ParagraphStyle('C3', fontName='Helvetica', fontSize=10, textColor=TEXT_DARK)),
            Paragraph("N86005084", ParagraphStyle('C4', fontName='Helvetica-Bold', fontSize=10, textColor=TEXT_DARK))
        ],
        [
            Paragraph("Ascione Nicola", ParagraphStyle('C5', fontName='Helvetica', fontSize=10, textColor=TEXT_DARK)),
            Paragraph("N86005133", ParagraphStyle('C6', fontName='Helvetica-Bold', fontSize=10, textColor=TEXT_DARK))
        ]
    ]

    t_candidati = Table(candidati_data, colWidths=[10 * cm, 6 * cm])
    t_candidati.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), ACCENT_BG),
        ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")),
        ('INNERGRID', (0,0), (-1,-1), 0.5, colors.HexColor("#E2E8F0")),
        ('TOPPADDING', (0,0), (-1,-1), 8),
        ('BOTTOMPADDING', (0,0), (-1,-1), 8),
        ('LEFTPADDING', (0,0), (-1,-1), 14),
        ('RIGHTPADDING', (0,0), (-1,-1), 14),
    ]))
    story.append(t_candidati)

    story.append(Spacer(1, 2.5 * cm))
    story.append(Paragraph("<b>Anno Accademico 2025/2026</b>", ParagraphStyle('Anno', fontName='Helvetica', fontSize=10, alignment=1, textColor=colors.HexColor("#666666"))))
    story.append(PageBreak())

    # ==================== PARTE 1 ====================
    story.append(Paragraph("1. Introduzione ed Obiettivi del Progetto", h1_style))
    story.append(Paragraph(
        "Il presente elaborato descrive l'architettura software, le decisioni progettuali e le soluzioni algoritmiche adottate per la realizzazione di un sistema distribuito per il gioco della <b>Battaglia Navale</b> in ambiente di rete, conforme ai requisiti dell'esame di <i>Laboratorio di Sistemi Operativi</i> (ordinamento da 6 CFU).",
        body_style
    ))
    story.append(Paragraph(
        "In conformità ai vincoli per l'ordinamento da 6 CFU, l'intero sistema è implementato in linguaggio <b>C standard (C11 / POSIX.1-2008)</b> sia per la componente Server che per la componente Client, senza ricorrere a container Docker e basandosi rigorosamente sulle primitive fornite dal sistema operativo Linux.",
        body_style
    ))
    story.append(Paragraph(
        "Gli obiettivi prioritari perseguiti durante la progettazione sono stati:",
        body_style
    ))
    story.append(Paragraph("• <b>Autoritarietà del Server (Server-Authoritative):</b> la logica di gioco, lo stato delle navi e la validità delle mosse risiedono esclusivamente sul Server. Il Client funge da interfaccia di presentazione, prevenendo incoerenze o manomissioni delle griglie.", bullet_style))
    story.append(Paragraph("• <b>Concorrenza Parallela:</b> gestione contemporanea di molteplici giocatori e sessioni di gioco indipendenti mediante Thread POSIX nativi (<code>pthread</code>).", bullet_style))
    story.append(Paragraph("• <b>Robustezza Estrema (Hardening Difensivo):</b> blindatura del codice contro input utente o di rete errati o malformati (fuzzing), prevenendo crash, memory leak o loop infiniti.", bullet_style))
    story.append(Paragraph("• <b>Pulizia ed Eleganza Architetturale:</b> codice lineare, modulare e conforme agli standard UNIX/POSIX per l'elaborazione di sistema.", bullet_style))

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("2. Architettura di Rete e Comunicazione Socket TCP", h1_style))
    story.append(Paragraph(
        "La comunicazione tra i processi Client e Server avviene mediante la <b>Socket API POSIX</b> su protocollo TCP. La scelta di TCP rispetto a UDP è motivata dalla natura orientata alla connessione, sequenziale e affidabile del protocollo (<code>SOCK_STREAM</code>), essenziale per uno scambio deterministico di comandi a turni.",
        body_style
    ))
    story.append(Paragraph(
        "Dettagli di configurazione e primitive utilizzate:",
        body_style
    ))
    story.append(Paragraph("• <b>Lato Server:</b> viene creato un endpoint con <code>socket(AF_INET, SOCK_STREAM, 0)</code>. Per evitare l'errore <i>'Address already in use'</i> in caso di riavvio rapido del processo, viene abilitata l'opzione <code>SO_REUSEADDR</code> tramite <code>setsockopt()</code>. L'indirizzo locale viene configurato impostando <code>sin_family = AF_INET</code>, <code>sin_port = htons(PORT)</code> e <code>sin_addr.s_addr = htonl(INADDR_ANY)</code>, per poi invocare <code>bind()</code> e porre il descrittore in ascolto con <code>listen(server_fd, 16)</code>.", bullet_style))
    story.append(Paragraph("• <b>Lato Client:</b> per specificare l'indirizzo del server si impiega la funzione standard POSIX <code>inet_aton()</code>, che converte una stringa IPv4 in formato dotted nella corrispondente struttura binaria di rete. La connessione viene stabilita mediante <code>connect()</code>.", bullet_style))

    # Box di approfondimento
    note_box_data = [[
        Paragraph("<b>Approfondimento Tecnico sulle Socket POSIX:</b><br/><i>«Il socket definisce un canale di comunicazione bidirezionale a flusso continuo. SOCK_STREAM garantisce integrità, ordine di consegna e assenza di duplicazioni dei byte. Il server chiama bind per associare l'indirizzo al descrittore, listen per predisporre la coda delle richieste e accept per creare il canale dedicato a ciascun client connesso.»</i>", box_text)
    ]]
    t_box = Table(note_box_data, colWidths=[16.5 * cm])
    t_box.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), ACCENT_BG),
        ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#005A9C")),
        ('TOPPADDING', (0,0), (-1,-1), 6),
        ('BOTTOMPADDING', (0,0), (-1,-1), 6),
        ('LEFTPADDING', (0,0), (-1,-1), 10),
        ('RIGHTPADDING', (0,0), (-1,-1), 10),
    ]))
    story.append(Spacer(1, 0.2 * cm))
    story.append(t_box)

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("3. Modello di Concorrenza: Server Multithreaded", h1_style))
    story.append(Paragraph(
        "Per gestire più client in parallelo (requisito vincolante della traccia), il server implementa il modello <b>Multithreaded</b>.",
        body_style
    ))
    story.append(Paragraph("<b>Confronto Thread POSIX vs Processi (fork):</b><br/>"
        "La duplicazione di processi tramite <code>fork()</code> genera spazi di indirizzamento separati e non condivisi. In un'applicazione come la Battaglia Navale, dove due client interagiscono sulla medesima partita, l'uso di <code>fork()</code> avrebbe richiesto complessi meccanismi di memoria condivisa POSIX (<code>shm_open</code>, <code>mmap</code>) e semafori IPC di sistema. I <b>Thread POSIX (`pthread`)</b> condividono naturalmente la memoria del processo padre, riducendo l'overhead di context-switch e consentendo una sincronizzazione rapida ed efficiente tramite Mutex in memoria.",
        body_style
    ))
    story.append(Paragraph("<b>Gestione della Terminazione con `pthread_detach()`:</b><br/>"
        "I thread terminati per cui non viene invocata <code>pthread_join()</code> rimangono residenti in memoria nello stato di <i>'zombie'</i>. Per scongiurare questo consumo di risorse senza costringere il thread principale a chiamate bloccanti, all'avvio della funzione worker viene eseguita la chiamata: <br/>"
        "<font color='#005A9C'><code>pthread_detach(pthread_self());</code></font><br/>"
        "In questo modo il sistema operativo dealloca automaticamente lo stack e i descrittori del thread al momento della sua uscita.",
        body_style
    ))

    story.append(PageBreak())

    # ==================== PARTE 2 ====================
    story.append(Paragraph("4. Sincronizzazione tra Thread e Prevenzione dei Deadlock", h1_style))
    story.append(Paragraph(
        "Poiché i thread worker accedono concorrentemente a variabili e strutture condivise (la tabella delle stanze, lo stato delle scacchiere, i socket dei giocatori), la sincronizzazione è indispensabile per prevenire <b>Race Conditions</b>.",
        body_style
    ))
    story.append(Paragraph("<b>Granularità dei Mutex Adottata:</b>", h2_style))
    story.append(Paragraph("1. <b>Mutex Globale della Lobby (<code>g_lobby_mutex</code>):</b> inizializzato staticamente tramite la macro <code>PTHREAD_MUTEX_INITIALIZER</code>. Protegge l'accesso all'elenco dei client connessi e la creazione/ricerca delle stanze di gioco.", bullet_style))
    story.append(Paragraph("2. <b>Mutex Dinamico di Partita (<code>Game.lock</code>):</b> allocato in ogni struttura di match e inizializzato dinamicamente con <code>pthread_mutex_init()</code>. La granularità fine garantisce che le operazioni interne a una partita (es. verifica di un colpo) non blocchino i thread che operano su altre partite indipendenti.", bullet_style))
    story.append(Paragraph("3. <b>Mutex di Scrittura su Socket (<code>ClientSession.send_lock</code>):</b> le socket TCP sono full-duplex, consentendo lettura e scrittura simultanee. Tuttavia, se due thread tentano di scrivere contemporaneamente sullo stesso socket client (ad esempio durante una notifica asincrona di turno o di colpo), i flussi di byte potrebbero sovrapporsi. Il lock <code>send_lock</code> serializza le chiamate a <code>send_line()</code> rendendo atomico ogni messaggio trasmesso.", bullet_style))

    story.append(Spacer(1, 0.2 * cm))
    story.append(Paragraph("<b>Risoluzione Formale del Deadlock:</b>", h2_style))
    story.append(Paragraph(
        "Per prevenire il deadlock è stata definita una gerarchia rigida e unidirezionale di acquisizione dei lock:",
        body_style
    ))
    story.append(Paragraph("• Se un'operazione richiede sia il lock della Lobby che il lock di una Partita, viene acquisito <b>sempre per primo <code>g_lobby_mutex</code></b> e solo successivamente <code>Game.lock</code>.", bullet_style))
    story.append(Paragraph("• Il rilascio avviene rigorosamente nell'ordine inverso.", bullet_style))
    story.append(Paragraph("• Non esiste alcun punto nel programma in cui un thread tenta di acquisire il lock della lobby mentre detiene già il lock di una partita, escludendo matematicamente ogni possibilità di attesa circolare.", bullet_style))

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("5. Multiplazione dell'I/O nel Client: System Call `select()`", h1_style))
    story.append(Paragraph(
        "Nel Client, quando un giocatore crea una partita, il programma deve rimanere in attesa che uno sfidante si colleghi (evento proveniente dal socket di rete), ma deve anche consentire all'utente di digitare un comando da tastiera (es. annullare la stanza digitando <code>cancel</code> o <code>0</code>).",
        body_style
    ))
    story.append(Paragraph(
        "Se si utilizzasse una normale chiamata di lettura bloccante come <code>fgets()</code> o <code>read(STDIN_FILENO)</code>, il processo rimarrebbe congelato in attesa della tastiera, impossibilitato a ricevere le notifiche di rete in arrivo dal server. "
        "Per monitorare contemporaneamente più descrittori si impiega la system call POSIX <b><code>select()</code></b>:",
        body_style
    ))

    # Tabella Select
    select_data = [
        [
            Paragraph("<b>Primitiva POSIX</b>", ParagraphStyle('Th1', fontName='Helvetica-Bold', fontSize=9, textColor=PRIMARY)),
            Paragraph("<b>Ruolo nel Client</b>", ParagraphStyle('Th2', fontName='Helvetica-Bold', fontSize=9, textColor=PRIMARY))
        ],
        [
            Paragraph("<code>fd_set readfds;</code>", code_inline),
            Paragraph("Insieme dei descrittori di file da controllare in lettura.", body_style)
        ],
        [
            Paragraph("<code>FD_ZERO(&readfds);</code>", code_inline),
            Paragraph("Azzera l'insieme dei descrittori all'inizio di ogni iterazione.", body_style)
        ],
        [
            Paragraph("<code>FD_SET(STDIN_FILENO, &readfds);</code><br/><code>FD_SET(sock, &readfds);</code>", code_inline),
            Paragraph("Inserisce sia la tastiera (file descriptor 0) che il socket di rete nell'insieme da monitorare.", body_style)
        ],
        [
            Paragraph("<code>select(maxfd + 1, &readfds, ...);</code>", code_inline),
            Paragraph("Blocca il processo fino a quando almeno uno dei due canali è pronto per la lettura.", body_style)
        ],
        [
            Paragraph("<code>FD_ISSET(fd, &readfds)</code>", code_inline),
            Paragraph("Verifica quale descrittore ha generato l'evento ed esegue l'azione opportuna.", body_style)
        ]
    ]
    t_select = Table(select_data, colWidths=[6.5 * cm, 10.0 * cm])
    t_select.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), colors.HexColor("#E2E8F0")),
        ('BOX', (0,0), (-1,-1), 1, colors.HexColor("#CBD5E1")),
        ('INNERGRID', (0,0), (-1,-1), 0.5, colors.HexColor("#E2E8F0")),
        ('TOPPADDING', (0,0), (-1,-1), 5),
        ('BOTTOMPADDING', (0,0), (-1,-1), 5),
        ('LEFTPADDING', (0,0), (-1,-1), 8),
        ('RIGHTPADDING', (0,0), (-1,-1), 8),
    ]))
    story.append(t_select)
    story.append(Paragraph(
        "Questa soluzione evita l'overhead di dover creare un secondo thread nel client, garantendo semplicità e massima reattività.",
        body_style
    ))

    story.append(PageBreak())

    # ==================== PARTE 3 ====================
    story.append(Paragraph("6. Trattamento dei Segnali Asincroni (`SIGPIPE` e `SIGINT`)", h1_style))
    story.append(Paragraph(
        "In ambiente UNIX la gestione dei segnali è indispensabile per garantire la robustezza delle applicazioni di rete.",
        body_style
    ))
    story.append(Paragraph("<b>Il Rischio di SIGPIPE e la sua Neutralizzazione:</b><br/>"
        "Nelle comunicazioni via socket TCP, se un processo tenta di scrivere su un descrittore il cui estremo remoto è stato chiuso improvvisamente (ad esempio per arresto forzato del client o perdita di connettività), il kernel genera il segnale <code>SIGPIPE</code>. L'azione predefinita di <code>SIGPIPE</code> in ambiente UNIX è la terminazione immediata del processo.<br/>"
        "Per proteggere il server:<br/>"
        "1. All'avvio del main viene impostato: <code>signal(SIGPIPE, SIG_IGN);</code>.<br/>"
        "2. Nelle chiamate di invio viene specificato il flag <code>MSG_NOSIGNAL</code> in <code>send()</code>.<br/>"
        "La scrittura fallita restituisce semplicemente <code>-1</code> con <code>errno == EPIPE</code>, consentendo al thread worker di intercettare l'errore e procedere alla rimozione pulita del client senza alcun crash del server.",
        body_style
    ))
    story.append(Paragraph("<b>Gestione di SIGINT (Ctrl+C):</b><br/>"
        "Sia nel server che nel client viene registrata una funzione di gestione (<code>sigaction</code>) per <code>SIGINT</code>. Alla pressione di Ctrl+C, il signal handler imposta una variabile di stato atomica (<code>g_server_running = 0</code>) e chiude i socket di ascolto, consentendo al processo di deallocare ordinatamente le strutture in memoria e terminare con stato 0.",
        body_style
    ))

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("7. Robustezza e Gestione Difensiva dell'Input (Hardening Anti-Break)", h1_style))
    story.append(Paragraph(
        "Il software è stato ingegnerizzato con un approccio <b>completamente difensivo</b> per resistere a test di rottura, fuzzing e valori fuori limite.",
        body_style
    ))
    story.append(Paragraph("<b>1. Eliminazione delle Vulnerabilità da Terminale (`safe_read_line`):</b><br/>"
        "Le funzioni canoniche come <code>scanf(\"%d\")</code> o <code>gets()</code> presentano vulnerabilità critiche: se l'utente digita caratteri non numerici (es. <code>abc</code>), <code>scanf</code> fallisce senza rimuovere i caratteri da <code>stdin</code>, innescando loop infiniti di errore. "
        "È stata pertanto sviluppata la funzione <code>safe_read_line()</code>:<br/>"
        "• Legge da terminale tramite <code>fgets()</code> su buffer fisso;<br/>"
        "• Se l'utente digita una riga più lunga del buffer, un ciclo iterativo su <code>getchar()</code> <b>scarica e distrugge tutti i caratteri eccedenti su stdin</b> fino al newline, impedendo che 'sporchino' le letture successive;<br/>"
        "• La conversione numerica è affidata a <code>strtol()</code> con verifica di <code>endptr</code> ed <code>errno</code>: qualsiasi carattere non numerico, numero negativo o valore fuori range viene rilevato e rifiutato con un messaggio d'errore chiaro.",
        body_style
    ))
    story.append(Paragraph("<b>2. Protezione del Server da Fuzzing e Buffer Poisoning (`recv_line`):</b><br/>"
        "Anche sulla rete un interlocutore malevolo (es. script automatizzato o netcat) potrebbe trasmettere righe prive di newline lunghe migliaia di byte. "
        "La funzione di ricezione <code>recv_line()</code> legge carattere per carattere fino a <code>\\n</code>: se la riga supera la dimensione massima consentita, i byte in eccesso vengono drenati e scartati. In tal modo il server non subisce alcun buffer overflow e lo stream TCP resta sincronizzato per il comando successivo.",
        body_style
    ))
    story.append(Paragraph("<b>3. Macchina a Stati Finiti Zero-Trust:</b><br/>"
        "Il server non confida mai nello stato del client. Ciascun comando è valido esclusivamente se il client si trova nello stato FSM appropriato (ad es. un colpo <code>FIRE</code> viene categoricamente respinto se il client si trova nella lobby o se non è il suo turno).",
        body_style
    ))

    story.append(PageBreak())

    # ==================== PARTE 4 ====================
    story.append(Paragraph("8. Logica di Gioco e Modulo Scacchiera", h1_style))
    story.append(Paragraph(
        "Le meccaniche della Battaglia Navale sono incapsulate nel modulo <code>board.c</code>, garantendo netta separazione tra networking e logica applicativa:",
        body_style
    ))
    story.append(Paragraph("• <b>Griglia di Gioco:</b> matrice 10x10 rappresentata dall'enumerazione <code>CellState</code> (<code>CELL_EMPTY</code>, <code>CELL_SHIP</code>, <code>CELL_MISS</code>, <code>CELL_HIT</code>, <code>CELL_SUNK</code>).", bullet_style))
    story.append(Paragraph("• <b>Composizione della Flotta (5 navi):</b> 1 Portaerei (5 caselle), 1 Corazzata (4 caselle), 1 Incrociatore (3 caselle), 1 Sottomarino (3 caselle), 1 Cacciatorpediniere (2 caselle).", bullet_style))
    story.append(Paragraph("• <b>Validazione del Piazzamento:</b> la funzione <code>board_can_place_ship()</code> accerta che le coordinate siano comprese tra 0 e 9, che la nave non superi i bordi (orizzontale o verticale) e che nessuna delle celle occupate sia già impegnata da un'altra nave (assenza di collisioni).", bullet_style))
    story.append(Paragraph("• <b>Risoluzione dei Colpi e Determinazione Vittoria:</b> la funzione <code>board_receive_shot()</code> aggiorna lo stato della cella. Se una nave subisce colpi pari alla sua dimensione, viene marcata come affondata (<code>CELL_SUNK</code>) e viene decrementato il contatore delle navi vive. Quando <code>ships_alive == 0</code>, il server dichiara la fine della partita.", bullet_style))

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("9. Gestione dei Casi Limite e Requisiti Opzionali", h1_style))
    story.append(Paragraph("<b>Gestione della Disconnessione dell'Avversario (Requisito Opzionale pag. 3):</b>", h2_style))
    story.append(Paragraph(
        "La traccia d'esame riporta a pagina 3 il requisito opzionale: <i>«se un giocatore cade, l'altro giocatore può decidere di terminare la partita o aspettare che un altro giocatore si unisca»</i>. "
        "Questa funzionalità è stata implementata come segue:",
        body_style
    ))
    story.append(Paragraph("1. Nel momento in cui un client interrompe la connessione (rilevata da <code>recv_line() <= 0</code> o errore di socket), il thread worker invoca <code>lobby_remove_client()</code>.", bullet_style))
    story.append(Paragraph("2. Se la partita era in corso, il giocatore rimasto riceve immediatamente la notifica speciale <code>OPPONENT_DISCONNECTED</code> e transita nello stato <code>STATE_OPPONENT_LEFT</code>.", bullet_style))
    story.append(Paragraph("3. Al giocatore superstite viene mostrato un menu decisionale interattivo:<br/>"
        "  - <b>Opzione 1 (Vittoria a tavolino):</b> la partita viene chiusa, viene assegnata la vittoria formale per abbandono e l'utente torna alla lobby.<br/>"
        "  - <b>Opzione 2 (Attesa nuovo giocatore):</b> la stanza viene resettata e rimessa nello stato di attesa (<code>STATE_WAITING_OPPONENT</code>), permettendo a un nuovo utente collegato di unirsi alla partita.",
        bullet_style
    ))
    story.append(Paragraph("<b>Meccanismo di Rivincita (Rematch):</b>", h2_style))
    story.append(Paragraph(
        "Al termine della sfida, entrambi i giocatori possono esprimere la preferenza di disputare una nuova partita mantenendo la stessa coppia. "
        "Se entrambi accettano (<code>REMATCH 1</code>), il server azzera le scacchiere e riavvia la fase di piazzamento; qualora uno dei due rifiuti (<code>REMATCH 0</code>), entrambi vengono reindirizzati alla lobby principale in totale coerenza di stato.",
        body_style
    ))

    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph("10. Conclusioni e Verifica Sperimentale", h1_style))
    story.append(Paragraph(
        "Il progetto è stato collaudato sia manualmente su più terminali che mediante uno script automatizzato di stress-testing e fuzzing. I risultati hanno confermato:",
        body_style
    ))
    story.append(Paragraph("• <b>Compilazione a zero warning</b> con i flag più severi del compilatore: <code>gcc -Wall -Wextra -Wpedantic -std=c11</code>.", bullet_style))
    story.append(Paragraph("• <b>Assenza di Memory Leak e Corruzioni:</b> collaudato con successo con <code>AddressSanitizer</code> (target <code>make debug</code> con <code>-fsanitize=address,undefined</code>).", bullet_style))
    story.append(Paragraph("• <b>Comportamento Deterministico:</b> perfetta coordinazione dei turni e immunità totale a disconnessioni forzate o input errati.", bullet_style))
    story.append(Paragraph(
        "L'architettura sviluppata soddisfa integralmente tutte le specifiche della traccia, dimostrando piena padronanza della programmazione concorrente e dei costrutti di sistema operativi in ambiente UNIX/Linux.",
        body_style
    ))

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"[OK] Documento PDF generato con successo: {filename}")


if __name__ == '__main__':
    out_path = "/home/manuel/Scrivania/ProgettoLSO/ProgettoLSO/doc/documentazione.pdf"
    if len(sys.argv) > 1:
        out_path = sys.argv[1]
    build_pdf(out_path)
