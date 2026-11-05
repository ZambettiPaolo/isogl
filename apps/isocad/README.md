# isocad

App CAD 2D tecnica minimale, orientata alla stampa, costruita sopra **isogl**.

L'applicazione possiede il documento (layer ed entità) e la finestra/contesto;
`libisogl` possiede le norme ISO, il modello in GPU e il rendering.

## Build

Dalla radice del monorepo:

```sh
make PREFIX="$HOME/.local" all install
make -C apps/isocad
./apps/isocad/build/isocad
```

In alternativa, senza installare in `$HOME`:

```sh
make PREFIX="$PWD/_install" all install
make -C apps/isocad PKG_CONFIG_PATH="$PWD/_install/lib/pkgconfig"
```

L'app consuma la libreria e l'helper SDL **solo** via `pkg-config`
(`isogl-sdl`); non compila i sorgenti di isogl né `glad.c`.

L'helper `hosts/isogl-sdl` possiede finestra, contesto e swap. A ogni frame
isocad chiama `isogl_sdl_host_begin_frame()`, che aggiorna il drawable con
`SDL_GetWindowSizeInPixels`, imposta il viewport e riasserisce lo stato GL. Gli
eventi SDL sono in punti logici: `isogl_sdl_host_local_to_pixels()` li converte
in pixel di framebuffer prima di pick, conversione world e zoom/pan. Il box di
pick è `ISOGL_PICK_DEFAULT_EXTENT * dpr` pixel fisici, quindi resta costante in
punti logici su display HiDPI.

## Run

```sh
./apps/isocad/build/isocad                    # default asset dir della libreria
./apps/isocad/build/isocad --assets DIR       # radice esplicita di shaders/fonts
./apps/isocad/build/isocad --assets DIR --pick-max N
```

`--pick-max N` (default 5) è la soglia di ambiguità: se il box di pick
(8 punti logici, convertiti in pixel fisici; texture `2×extent`) contiene più
di N ID, l’app stampa “ambiguo” e invita a ingrandire. A zoom basso molti
oggetti cadono negli stessi pochi pixel: il pre-filtro di distanza non li può
separare, perché sono davvero vicini. La soglia è la politica; il filtro serve
solo a scartare i falsi positivi geometrici.

Comandi:

- rotella: zoom ancorato al cursore
- clic sinistro senza trascinare: pick (box 8 punti logici) e stampa degli ID
- drag sinistro: selezione d’area; sinistra→destra = window, destra→sinistra = crossing
- drag con tasto centrale: pan
- clic destro: pick e stampa degli ID (scorciatoia)
- `Ctrl+F`: fit del foglio nell’area sopra la console
- `Ctrl+T`: alterna colori-spessore / colori-stampa
- `Ctrl+Q`: esci
- `Esc`: se la riga di comando contiene testo lo svuota; altrimenti annulla il
  drag e svuota la selezione
- testo digitato: va alla riga di comando, sempre attiva; `Enter` echeggia la
  riga nelle 3 righe di storico; `Backspace` cancella

Il pannello inferiore è un prototipo: una striscia opaca di **100 px logici**
con **4 righe** (3 di storico + input) disegnate col font vettoriale. La scena
è adattata sopra il pannello modificando la matrice modello, senza un secondo
viewport. I comandi reali non sono ancora implementati: `Enter` fa solo eco.
Click e rotella dentro il pannello vengono ignorati dalla scena.

Durante il drag di selezione `cad` ricalcola il set esatto a ogni movimento:
`cad_select_rect()` per le entità CAD e `isogl_symbol_hit()` per i simboli
isogl. Il set è **taggato** (`CAD_SEL_ENTITY` / `CAD_SEL_SYMBOL`); le entità
CAD ricevono i flag con `cad_selection_apply`, i simboli con
`isogl_symbol_set_selected`. L’evidenziazione è per entità/simbolo intero, non
per segmento: in crossing vengono selezionati quelli che intersecano il
rettangolo, in window solo quelli interamente dentro. Il colore segue il modo
(giallo crossing, verde window) e il rettangolo resta un overlay rubber-band.
Al rilascio il set viene conservato e resta evidenziato nei frame successivi,
anche dopo pan/zoom; `Esc` lo svuota. Il predicato per segmento nel GS resta
disponibile come anteprima veloce, ma in isocad è disattivato
(`isogl_set_selection_preview(0)`), così anteprima e set finale coincidono.
Il pan centrale non interrompe la selezione: il rettangolo segue la vista.
Al rilascio i riferimenti vengono stampati nel terminale come `E<id>` (entità
CAD) e `S<id>` (simbolo isogl). L’OBB del testo è calcolato dagli advance reali
del font (`isogl_font_text_advance`). Il click singolo stampa ancora il pick e
non aggiorna il set persistente: la politica topmost/centro è un passo
successivo.

Il pick copre i VBO dei segmenti (linee, cerchi, rettangoli), le stringhe di
testo e i simboli isogl. Il testo viene disegnato nel pick pass con un
programma font dedicato; i simboli hanno un programma/pick IDs propri. Gli ID
delle entità CAD sono `CadEntity.id`, assegnati da un contatore monotono
(`CadDocument.next_id`), non più dall’ordine di creazione; gli ID dei simboli
sono un namespace separato (`isogl_symbol_id`). La texture di pick
(`2×extent` per lato) viene letta a spirale dal centro verso l’esterno, quindi
il primo ID stampato è il candidato più vicino al cursore.

## Quota

La quota allineata è un **simbolo isogl**, non un’entità CAD. Il CAD costruisce
ogni volta un `IsoglDimensionDesc` completo:

- `p1`, `p2`: punti misurati; il valore è `|p1-p2|`, senza unità automatica;
- `p3`: punto direzione/lato della linea di quota;
- `distance`: offset della linea di quota in unità documento (0 = ricavato da `p3`);
- `text_offset_mm`: posizione del testo rispetto al centro, in mm di carta e con
  segno lungo la linea;
- `text_left`/`text_right`: testo libero prima/dopo il valore (per esempio `Ø `
  a sinistra o ` mm` a destra);
- `arrow_left`/`arrow_right`: `NONE`, `OPEN`, `CLOSED`, `TICK`, `DOT`,
  `OPEN_OUTSIDE`, `CLOSED_OUTSIDE`; testo 2,5 mm, frecce 2,2 mm con ampiezza
  30°, pallino 1,5 mm. Le varianti `*_OUTSIDE` puntano all’interno da fuori
  del tratto; la linea di quota è estesa di 5 mm oltre l’estremo interessato;
- `mark_points`: disegna un segmento di riferimento da 0,5 mm che parte da
  `p1`/`p2` verso la linea di quota, prima del distacco di 3 mm;
- le linee di riferimento partono 3 mm dopo il punto misurato;
- precisione, altezza testo e `pick_text`.

Poi chiama `cad_add_dimension_symbol()` e conserva solo l’`isogl_symbol_id` nel
`CadDocument`.

- **render**: linea di quota, due linee di riferimento, frecce secondo il tipo
  scelto (0–3 segmenti ciascuna) e un run di testo; il VBO e la stringa
  condividono l’`entity_id`;
- **valore**: calcolato da `|p1-p2|`, mai memorizzato; le coordinate sono in
  unità documento, mentre altezza testo, frecce e sporgenze sono grandezze di
  carta convertite con `doc->sheet_scale`;
- **pick**: le stesse linee/frecce più l’OBB del testo se `pick_text != 0`;
- **selezione**: window = tutte le primitive contenute; crossing = almeno una
  contenuta o intersecante; `pick_text = 0` lascia il testo al solo click.

Il documento demo contiene quattro quote: diametro con frecce chiuse,
orizzontale con frecce esterne chiuse, verticale con pallini e testo spostato
lungo la linea, inclinata con frecce miste e testo a destra. Il center mark è
un secondo simbolo al centro del cerchio. La geometria arriva dal modulo puro condiviso
`isogl_symbol_geometry`; il registry `isogl_symbol` possiede VBO, stringa,
hit/pick ed explode. Per manipolare un
simbolo il CAD usa `isogl_symbol_explode`, che trasferisce segmenti e testo al
CAD e distrugge il simbolo.

## Simboli e ambiente di test

Il confine è: il CAD possiede la geometria di gruppi e primitive; isogl possiede
i simboli. Un simbolo ha un `isogl_symbol_id` opaco; il CAD può chiedere
`isogl_symbol_hit/bounds/pick` e, per manipolarlo, `isogl_symbol_explode`
(segmenti + testi, simbolo distrutto). Il CAD non legge i VBO interni del
simbolo.

La verifica usa:

- `tests/fake_cad.*`: fake CAD gerarchico (gruppi, primitive, riferimenti a
  simboli), non il modello di produzione;
- `tests/fixtures/test_drawing.toml`: disegno strutturato;
- `tests/test_symbols.c`: render, pick, hit, selezione, explode e
  trasferimento;
- `tests/test_symbol_geometry.c`: geometria pura headless.

```sh
make test-symbol-geometry
make test-symbols
make test
```

## Modello del documento

- `CadLayer`: nome, colore di stampa, peso e tipo di linea ISO predefiniti.
- `CadEntity`: linea, cerchio, rettangolo o testo, con layer, pattern e peso.
  Le quote e i center mark non sono entità CAD: sono simboli isogl.
- `CadDocument`: array di entità con `next_id` monotono, riferimenti ai simboli
  isogl (`isogl_symbol_id *symbols`) e specchio GPU (`isogl_vbo_array` per i
  segmenti, `isogl_string_list` per il testo).

Ogni entità geometrica ha il proprio VBO. Quando un'entità viene modificata
l'app ricostruisce solo la sua lista di segmenti e chiama
`isogl_vbo_array_update()`: il resto del modello resta in memoria video.

Il testo è incrementale per entità: ogni stringa ha un VBO completo di coppie
`GL_LINES`; `cad_add_text` crea solo la sua, `cad_update_text` e
`cad_set_text_transform` aggiornano una sola voce per `pick_id` (VBO nuovo solo
se cambia il contenuto, `string_matrix` se cambia solo la posizione).
`cad_document_rebuild_text` resta per ricarica font/rebuild globale. Anche un
simbolo si aggiorna in place con `isogl_symbol_update_*`, preservando ID, pick e
selezione. Memoria: ~0,5–0,8 KiB per stringa tipica e ~6–8 MB per 10.000
stringhe; a scala il limite sono i VBO e le draw call, non i byte.

## Stato attuale

Milestone A: foglio, linee, cerchi, rettangoli, testo vettoriale, pattern e pesi
ISO, zoom/pan, modalità colori-spessore/stampa, selezione d’area persistente
taggata CAD/simbolo, quota e center mark come simboli isogl con registry,
hit/pick/explode, ambiente di test gerarchico e testo incrementale per entità.
Editing, UI layer, archi, polilinee, export/stampa e modello CAD gerarchico
sono i passi successivi.
