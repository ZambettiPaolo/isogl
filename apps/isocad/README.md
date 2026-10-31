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

Durante il drag di selezione `cad` ricalcola il set esatto a ogni movimento con
`cad_select_rect()` e lo applica al renderer come flag per entità
(`cad_selection_apply`). L’evidenziazione è quindi per entità intera, non per
segmento: in crossing vengono selezionate le entità che intersecano il
rettangolo, in window solo quelle interamente dentro. Il colore segue il modo
(giallo crossing, verde window) e il rettangolo resta un overlay rubber-band.
Al rilascio il set viene conservato e resta evidenziato nei frame successivi,
anche dopo pan/zoom; `Esc` lo svuota. Il predicato per segmento nel GS resta
disponibile come anteprima veloce, ma in isocad è disattivato
(`isogl_set_selection_preview(0)`), così anteprima e set finale coincidono.
Il pan centrale non interrompe la selezione: il rettangolo segue la vista.
Al rilascio gli ID vengono anche stampati nel terminale. L’OBB del testo è
calcolato dagli advance reali del font (`isogl_font_text_advance`) e
l’highlight è per stringa intera, non più a frammenti. Il click singolo stampa
ancora il pick e non aggiorna il set persistente: la politica topmost/centro è
un passo successivo.

Il pick copre i VBO dei segmenti (linee, cerchi, rettangoli) e le stringhe di
testo. Il testo viene disegnato nel pick pass con un programma font dedicato
che scrive l’ID dell’entità. Gli ID stampati sono i `CadEntity.id`, cioè
l’ordine di creazione delle entità. La texture 16×16 viene letta a spirale dal
centro verso l’esterno, quindi il primo ID stampato è il candidato più vicino
al cursore; con un box logico 8×8 il pick copre più area del vecchio 5×5 e la
definizione doppia rende più affidabile l’aggancio delle linee sottili.

## Quota

La quota allineata è la prima entità composta. `CadDimension` conserva `p1`
(primo punto misurato), `p2` (secondo) e `p3` (un punto sulla linea di quota:
offset e lato), più prefisso, unità, suffisso, precisione, altezza testo e
`pick_text`.

- **render**: linea di quota, due linee di riferimento, quattro tratti delle
  punte e un run di testo; il VBO e la stringa condividono l’`entity_id`;
- **valore**: calcolato da `|p1-p2|`, mai memorizzato; le coordinate sono in
  unità documento, mentre altezza testo, frecce e sporgenze sono grandezze di
  carta convertite con `doc->sheet_scale`;
- **pick**: le stesse linee/punte più l’OBB del testo se `pick_text != 0`;
- **selezione**: window = tutte le primitive contenute; crossing = almeno una
  contenuta o intersecante; `pick_text = 0` lascia il testo al solo click.

La quota del documento demo misura il diametro del cerchio ed è centrata sotto
il cerchio.

## Modello del documento

- `CadLayer`: nome, colore di stampa, peso e tipo di linea ISO predefiniti.
- `CadEntity`: linea, cerchio, rettangolo o testo, con layer, pattern e peso.
- `CadDocument`: array di entità più lo specchio GPU (`isogl_vbo_array` per i
  segmenti, `isogl_string_list` per il testo).

Ogni entità geometrica ha il proprio VBO. Quando un'entità viene modificata
l'app ricostruisce solo la sua lista di segmenti e chiama
`isogl_vbo_array_update()`: il resto del modello resta in memoria video. Il
testo viene ricostruito interamente solo quando cambiano entità di testo.

## Stato attuale

Milestone A: foglio, linee, cerchi, rettangoli, testo vettoriale, pattern e pesi
ISO, zoom/pan, modalità colori-spessore/stampa, selezione d’area persistente,
quota allineata con geometria di render e di pick. Editing, UI layer, archi,
polilinee ed export/stampa sono i passi successivi.
