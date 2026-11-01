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

L'app consuma la libreria **solo** via `pkg-config`; non compila i sorgenti di
isogl né `glad.c`.

## Run

```sh
./apps/isocad/build/isocad                    # default asset dir della libreria
./apps/isocad/build/isocad --assets DIR       # radice esplicita di shaders/fonts
./apps/isocad/build/isocad --assets DIR --pick-max N
```

`--pick-max N` (default 5) è la soglia di ambiguità: se il box logico 8×8
(texture 16×16) contiene più di N ID, l’app stampa “ambiguo” e invita a
ingrandire. A zoom basso molti oggetti cadono negli stessi pochi pixel: il
pre-filtro di distanza non li può separare, perché sono davvero vicini. La
soglia è la politica; il filtro serve solo a scartare i falsi positivi
geometrici.

Comandi:

- rotella: zoom ancorato al cursore
- clic sinistro senza trascinare: pick 8×8 (texture 16×16) e stampa degli ID
- drag sinistro: selezione d’area; sinistra→destra = window, destra→sinistra = crossing
- drag con tasto centrale: pan
- clic destro: pick 8×8 (texture 16×16) e stampa degli ID (scorciatoia)
- `F`: fit del foglio
- `T`: alterna colori-spessore / colori-stampa
- `Esc`: annulla il drag e svuota la selezione
- `Q`: esci

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
sono un namespace separato (`isogl_symbol_id`). La texture 16×16 viene letta a
spirale dal centro verso l’esterno, quindi il primo ID stampato è il candidato
più vicino al cursore.

## Quota

La quota allineata è un **simbolo isogl**, non un’entità CAD. Il CAD costruisce
un `IsoglDimensionDesc` con `p1` (primo punto misurato), `p2` (secondo) e `p3`
(un punto sulla linea di quota: offset e lato), più prefisso, unità, suffisso,
precisione, altezza testo e `pick_text`; poi chiama
`cad_add_dimension_symbol()` e conserva solo l’`isogl_symbol_id` nel
`CadDocument`.

- **render**: linea di quota, due linee di riferimento, quattro tratti delle
  punte e un run di testo; il VBO e la stringa condividono l’`entity_id`;
- **valore**: calcolato da `|p1-p2|`, mai memorizzato; le coordinate sono in
  unità documento, mentre altezza testo, frecce e sporgenze sono grandezze di
  carta convertite con `doc->sheet_scale`;
- **pick**: le stesse linee/punte più l’OBB del testo se `pick_text != 0`;
- **selezione**: window = tutte le primitive contenute; crossing = almeno una
  contenuta o intersecante; `pick_text = 0` lascia il testo al solo click.

La quota del documento demo misura il diametro del cerchio ed è centrata sotto
il cerchio; il center mark è un secondo simbolo al centro del cerchio. La
geometria arriva dal modulo puro condiviso `isogl_symbol_geometry`; il registry
`isogl_symbol` possiede VBO, stringa, hit/pick ed explode. Per manipolare un
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
