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
- `Ctrl+F`: fit del foglio nell’area interna ai pannelli
- `Ctrl+T`: alterna colori-spessore / colori-stampa
- `F2`: workspace Disegno (set attuale)
- `F3`: workspace Modifica
- `F4`: workspace Annotazione
- `F5`: workspace Vista/Stampa
- `Ctrl+Q`: esci
- `Esc`: se la riga di comando contiene testo lo svuota; altrimenti annulla il
  drag e svuota la selezione
- testo digitato: va alla riga di comando, sempre attiva; `Enter` echeggia la
  riga nelle 3 righe di storico e, se riconosciuta, la esegue; `Backspace`
  cancella

### Comandi della console (prototipo)

I comandi operano sui record CAD dei simboli (`CadSymbolRef`: ID, tipo e copia
dei parametri) e usano solo i metadati pubblici `isogl_symbol_param_desc`. La
console mostra una riga di riepilogo; i dettagli e i valori aggiornati vanno su
stdout.

```text
sym list                  elenca S<id> <tipo> su stdout
sym info <id>             tipo, descrizione e ogni parametro: key, label,
                          help, unit, kind, count, valore/i
sym set <id> <key> ...    modifica un parametro e rigenera il simbolo
sym points <id>           elenca i punti di controllo con indici e label
sym remove <id>           distrugge il simbolo e rimuove il riferimento
save <path>               scrive una sezione schema v2 [[symbols]] + params
```

Forme di `sym set` per tipo di parametro:

```text
points <i> <x> <y>
texts <i> <testo libero fino a fine riga>
segments <i> <x0> <y0> <x1> <y1>
number <valore>
integer|enum <intero>
boolean 0|1|true|false
color <r> <g> <b> [<a>]
```

`SCALE` e i parametri con `editable = 0` sono rifiutati. Un aggiornamento
invalido lascia intatti simbolo, geometria e copia dei parametri: la console
lavora su una copia temporanea e chiama `cad_symbol_params_update`, che a sua
volta usa `isogl_symbol_params_update` (ID, pick ID e selezione preservati) e
poi rinfresca la copia del documento con un nuovo `params_get`.

Il pannello inferiore è un prototipo: una striscia opaca di **100 px logici**
con **4 righe** (3 di storico + input) disegnate col font vettoriale. Lo sfondo
è un quad come gli altri pannelli; il modulo console possiede solo il testo.
La scena è adattata al rettangolo interno ai pannelli modificando la matrice
modello, senza un secondo viewport. `Enter` esegue i comandi `sym`/`save`
descritti sopra; una riga non riconosciuta resta un semplice eco. Click e
rotella dentro un pannello vengono ignorati dalla scena.

Attorno all'area di disegno ci sono tre pannelli con icone, spessi **32 px
logici** (scalati con il device pixel ratio):

- in alto: documento e storico (`nuovo`, `apri`, `salva`, `undo`, `redo`,
  `stampa`);
- a sinistra: strumenti di disegno (selezione, linea, cerchio, rettangolo,
  polilinea, testo, quota, center mark);
- a destra: vista e visualizzazione (zoom +, zoom −, pan, fit, griglia, snap,
  spessore).

I pannelli sono quattro **workspace globali** selezionati da F2–F5: Disegno,
Modifica, Annotazione, Vista/Stampa. Tutti i dock cambiano insieme; la pagina
attiva resta nel dock, le altre sono gruppi retained trasformati fuori campo e
con `visible = 0`. Il cambio è istantaneo e il content rect/fit/input filtering
restano invariati; solo la pagina attiva riceve hover e tooltip.
`panel_deck.c` contiene la matematica pura di slot e parcheggio.

Tutti gli sfondi (alto, sinistro, destro e console), le icone, l'hover e il
tooltip passano da un unico **overlay a gruppi** (`isogl_overlay`). Il CAD crea
un gruppo per ogni pannello/icona elementare e ci mette primitive locali (quad,
linee, testo) con trasformazione, layer, clip rect e override di colore. Ogni
icona è quindi un gruppo di linee in coordinate locali; il layout imposta solo
la trasformazione, l'hover cambia il colore del gruppo e mostra un tooltip
(quad + testo) in un layer alto.

`isogl_overlay_commit()` ricostruisce solo i gruppi dirty e carica i VBO
condivisi; nei frame statici non fa nulla. `isogl_overlay_draw()` usa una
proiezione pixel propria e **non tocca** `isogl_model_matrix` e
`isogl_view_matrix`; il clip è per-vertice su quad/linee e per-stringa sul
testo, quindi non moltiplica le draw call. La console aggiunge solo le righe di
testo; il viewport isogl resta quello dell'intera finestra e il foglio viene
adattato al content rect dei pannelli con scala uniforme e centratura.

Trigger di aggiornamento: mouse over, input di testo, resize/DPR, cambio icone
(`clear` + re-add del gruppo), visibilità/layer/clip e movimento del tooltip.
Costo UI: layer 0 = 1 draw per tutti i quad, 1 per tutte le linee, 1 per
stringa (4 di console); layer 1 del tooltip = 1 quad + 1 testo. In totale circa
**6 draw call senza tooltip e 8 con tooltip**, contro le 14 della versione
precedente. Le linee dell'overlay restano `GL_LINES` a 1 px: non esiste un
helper che le converta in quad spessi.

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
del font (`isogl_font_text_advance`). Il click singolo sinistro, se il pick
restituisce almeno un simbolo, seleziona il primo (il candidato più vicino al
cursore), sostituisce il set persistente e mostra in console un riepilogo
`S<id> <tipo> <n>p`; il drag resta invariato. La politica topmost/centro tra
più simboli sovrapposti è ancora un passo successivo.

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

Poi chiama `cad_add_dimension_symbol()`, che crea il simbolo e registra nel
`CadDocument` un `CadSymbolRef { id, type, params }`: l’ID opaco, il puntatore
al tipo C persistente e una copia profonda dei parametri tipizzati. La copia è
il record CAD usato da console, salvataggio e futura UI proprietà; non è una
copia dei segmenti.

- **render**: linea di quota, due linee di riferimento, frecce secondo il tipo
  scelto (0–3 segmenti ciascuna) e un run di testo; il VBO e la stringa
  condividono l’`entity_id`;
- **valore**: calcolato da `|p1-p2|`, mai memorizzato; le coordinate sono in
  unità documento, mentre altezza testo, frecce e sporgenze sono grandezze di
  carta convertite con la scala foglio del modulo (`isogl_symbol_get_sheet_scale`,
  default 1.0). `doc->sheet_scale` viene ancora passato nel descrittore per
  compatibilità, ma il registry ignora quell'argomento;
- **pick**: le stesse linee/frecce più l’OBB del testo se `pick_text != 0`;
- **selezione**: window = tutte le primitive contenute; crossing = almeno una
  contenuta o intersecante; `pick_text = 0` lascia il testo al solo click.

Il documento demo contiene quattro quote: diametro con frecce chiuse,
orizzontale con frecce esterne chiuse, verticale con pallini e testo spostato
lungo la linea, inclinata con frecce miste e testo a destra. Il center mark è
un secondo simbolo al centro del cerchio. La geometria arriva dal modulo puro condiviso
`isogl_symbol_geometry`; il registry `isogl_symbol` possiede VBO, stringa,
hit/pick ed explode. Per modificare un simbolo il CAD richiama
`isogl_symbol_update_dimension`/`update_center_mark` con un descrittore
completo: ID, pick e selezione restano invariati. Per trasferire la geometria
al CAD usa `isogl_symbol_explode`, che emette segmenti e testo e distrugge il
simbolo.

### Contratto reale e lacune

isogl possiede record, copia dei parametri, VBO e glyph string; il CAD
possiede solo l'`isogl_symbol_id` opaco. I simboli sono **tipi definiti in C**:
il tipo dichiara schema dei parametri, descrizioni per l'editor, validazione e
generatore puro. Il CAD può leggere metadati e valori con
`isogl_symbol_type_at` / `isogl_symbol_params_get`, modificare con
`isogl_symbol_params_update` (ID, pick e selezione preservati) e usare
`hit/bounds/pick_at/get_selected`; non legge VBO, stringhe o geometria interna.
`pick_at` e `draw_all` operano sul registry, non su `CadDocument.symbols`.
`bounds` deriva dalla geometria di pick: con `pick_text = 0` il testo non è
incluso. `dimension` e `center_mark` sono adapter sui generatori puri esistenti;
un simbolo nuovo richiede un nuovo tipo C registrato con
`isogl_symbol_register_type`.

**Scala foglio globale.** `isogl_symbol_set_sheet_scale(scale)` (valori finiti
e > 0) è l'unica autorità: tutti i simboli vivi usano quel valore e la
rigenerazione conserva ID, pick ID e selezione. `params_get` espone il
parametro `SCALE` in sola lettura con la scala applicata; `params_update`
ignora un eventuale `SCALE` in ingresso. Se un simbolo non riesce a
rigenerarsi, conserva geometria e scala precedenti; il setter prosegue con gli
altri e restituisce `-1` se almeno uno ha fallito. `isocad` imposta la scala
globale da `doc->sheet_scale` prima di creare i simboli; gli argomenti di scala
dei wrapper restano ignorati.

**Aggiornamento atomico per simbolo.** `params_update` prepara nuova
geometria, tutte le glyph string e la copia dei parametri prima di mutare il
simbolo; crea un VBO nuovo e accoda le stringhe con `pick_id` temporaneo 0.
Solo al commit rimuove vecchie stringhe/VBO, promuove le nuove al `pick_id`
reale, ripristina la selezione e sostituisce parametri e pick item. Un
fallimento di validazione, generazione o allocazione lascia intatto lo stato
precedente. Limite residuo: gli array condivisi di VBO e stringhe non sono
transazionali a livello globale, quindi un cambio di scala può riuscire per
alcuni simboli e fallire per altri; l'atomicità è per singolo simbolo.

Stato in isocad: il documento conserva `CadSymbolRef { id, type, params }`
con copia profonda dei parametri; `cad_symbol_find`,
`cad_symbol_update_params` e `cad_remove_symbol` gestiscono il record. La
console espone `sym list/info/set/points/remove` e `save <path>`. Il writer
schema v2 è **save-only**: usa solo stdio e i metadati pubblici, scrive
`[[symbols]]` con `type`, `group = 0` e `[symbols.params]`, omette `SCALE` ed è
l’unico formato prodotto. isocad non include un parser TOML e non carica un
disegno completo; il round-trip del file resta compito di
`examples/demo/drawing_load`.

Restano da implementare: pannello proprietà visuale e handle di editing
(la modifica oggi è solo da console); liste dinamiche; transazione globale
degli array condivisi; preservazione dei tipi sconosciuti nel writer;
caricamento di un disegno completo nell’app.

## Simboli e ambiente di test

Il confine è: il CAD possiede la geometria di gruppi e primitive; isogl possiede
i simboli. Un simbolo ha un `isogl_symbol_id` opaco; il CAD può chiedere
`isogl_symbol_hit/bounds/pick`, aggiornarlo in place con
`isogl_symbol_update_dimension/center_mark` e trasferirne la geometria con
`isogl_symbol_explode` (segmenti + testi, simbolo distrutto). Il CAD non legge
i VBO interni del simbolo.

La verifica usa:

- `tests/fake_cad.*`: fake CAD gerarchico (gruppi, primitive, riferimenti a
  simboli), non il modello di produzione;
- `tests/fixtures/test_drawing.toml`: disegno strutturato;
- `tests/test_symbols.c`: render, pick, hit, selezione, explode e
  trasferimento;
- `tests/test_symbol_geometry.c`: geometria pura headless.

```sh
make test-symbol-geometry   # geometria pura, headless
make test-console-cmd       # parser console, headless
make test-cad-symbols       # CadSymbolRef, update/remove/writer, round-trip, GL
make test-symbols
make test
```

## Modello del documento

- `CadLayer`: nome, colore di stampa, peso e tipo di linea ISO predefiniti.
- `CadEntity`: linea, cerchio, rettangolo o testo, con layer, pattern e peso.
  Le quote e i center mark non sono entità CAD: sono simboli isogl.
- `CadDocument`: array di entità con `next_id` monotono, record dei simboli
  isogl (`CadSymbolRef *symbols`, con ID, tipo e copia dei parametri) e
  specchio GPU (`isogl_vbo_array` per i segmenti, `isogl_string_list` per il
  testo).
- `CadSymbolRef`: vista CAD di un simbolo isogl. Il registry possiede VBO,
  stringhe e geometria; il documento possiede solo ID, puntatore al tipo C
  persistente e copia profonda dei parametri. L’ID resta stabile; la copia
  parametri si aggiorna con `cad_symbol_update_params` (nuovo `params_get`) e
  viene liberata con `cad_remove_symbol`/`cad_document_free`.

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

Milestone A + UI screen-space: foglio, linee, cerchi, rettangoli, testo
vettoriale, pattern e pesi ISO, zoom/pan, modalità colori-spessore/stampa,
selezione d’area persistente taggata CAD/simbolo, quota e center mark come
simboli isogl con registry, hit/pick/explode, record CAD con tipo+copia
parametri, modifica da console (`sym list/info/set/points/remove`, `save`),
ambiente di test gerarchico e testo incrementale per entità. L’interfaccia usa
un overlay a gruppi
(`isogl_overlay`) con pannelli parametrici (`isogl_panel`): pannelli, icone come
gruppi di linee locali, hover con tooltip e console come gruppo testo; 6–8 draw
call UI. I pannelli hanno **quattro workspace** (F2–F5): la pagina attiva sta
nel dock, le altre restano retained fuori campo e nascoste; il cambio è
istantaneo. Editing, azioni dei pulsanti, archi, polilinee, export/stampa e
modello CAD gerarchico sono i passi successivi.
