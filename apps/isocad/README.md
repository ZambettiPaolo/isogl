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
NUMBERS <i> <valore>          es. widths[i]
ENUM_LIST <i> <nome>|<intero> es. alignments[i]
number <valore>
integer <intero>
enum <nome>|<intero>      token stabile della tabella (es. closed) o valore
boolean 0|1|true|false
color <r> <g> <b> [<a>]
```

Gli `ENUM` e ogni elemento degli `ENUM_LIST` usano la tabella del descrittore:
`sym info <id>` elenca `token=etichetta(valore)` e i singoli elementi,
`sym set` accetta il token (case-insensitive) o un intero presente in tabella e
rifiuta gli altri. I `NUMBERS` si modificano con `<i> <valore>`, gli
`ENUM_LIST` con `<i> <token|intero>`. La tabella è anche il validatore: un
valore fuori tabella non arriva alla rigenerazione.

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

Il documento demo (A3 landscape, margine 10) contiene quattro quote allineate:
diametro con frecce chiuse, orizzontale con frecce esterne chiuse, verticale
con pallini e testo spostato lungo la linea, inclinata con frecce miste e testo
a destra. Il center mark è un secondo simbolo al centro del cerchio. La
geometria arriva dal modulo puro condiviso
`isogl_symbol_geometry`; il registry `isogl_symbol` possiede VBO, stringa,
hit/pick ed explode. Per modificare un simbolo il CAD richiama
`isogl_symbol_update_dimension`/`update_center_mark` con un descrittore
completo: ID, pick e selezione restano invariati. Per trasferire la geometria
al CAD usa `isogl_symbol_explode`, che emette segmenti e testo e distrugge il
simbolo.

La scena demo include anche i campioni delle forme e dei tipi nuovi, tutti
selezionabili:

- **ellisse CAD**: assi (45,195)-(105,195) e punto (75,170), cioè centro
  (75,195), rx 30, ry 25; layer 1, continuo, 0.35. Etichetta "ellisse" a
  (45,162), layer 3, altezza 3.5.
- **arco a tre punti**: centro (310,225), inizio (332,225), fine (310,247),
  quadrante di 90° CCW, r 22; layer 2, continuo, 0.5. Etichetta
  "arco 3 punti" a (288,258).
- **arco centro+angoli**: centro (365,205), raggio 25, da 200° a 300°
  (100° CCW); layer 1, tratteggiato, 0.25. Etichetta "arco angoli" a
  (378,192), spostata a sinistra del valore concordato per restare dentro il
  bordo del foglio.
- **riga di tabella** (`table_row`, costruita con l'API generica e parametri
  liberati subito): origine in basso a sinistra (30,135), altezza 10 mm,
  testo 3 mm, padding 1.5 mm, larghezze 38/34/28 mm, allineamenti
  left/center/right, testi "Materiale", "Ø 20", "2 pz"; layer 2, continuo,
  0.35. Etichetta "riga di tabella" a (30,127). L'origine è 5 mm più in basso
  del valore concordato (140) per non attraversare la linea d'asse orizzontale
  a y = 148.5.
- **quota diametro** (`dimension_diameter`) sul cerchio esistente (centro
  210,148.5, r 60): punto sul cerchio (270,148.5), ancoraggio testo (267,188)
  (2 mm a destra e 3 mm sopra il valore concordato, perché a (265,185) il
  glifo "Ø" entrava di circa 1 mm nel cerchio), prefisso "Ø", precisione 1,
  altezza testo 3 mm, frecce chiuse, `pick_text 1`; layer 2.
- **quota raggio** (`dimension_radius`) sullo stesso cerchio: punto a 135°
  ≈ (167.57,190.93), ancoraggio testo (140,219) oltre il punto sulla stessa
  semiretta, prefisso "R", precisione 1, altezza testo 3 mm, freccia chiusa,
  `pick_text 1`; layer 1.

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
incluso. `dimension`, `center_mark`, `dimension_diameter` e
`dimension_radius` sono adapter sui generatori puri esistenti;
`table_row` è il primo tipo con liste dinamiche (`NUMBERS`, `ENUM_LIST`) e vive
in `src/isogl_symbol_table.c`. Un simbolo nuovo richiede un nuovo tipo C
registrato con `isogl_symbol_register_type`, con la sua geometria pura e i
metadati nel file del tipo.

**Diametro (`dimension_diameter`) e raggio (`dimension_radius`).**
`src/isogl_symbol_diameter.c` e `src/isogl_symbol_radius.c` condividono tre
`points`: centro del cerchio, punto sul cerchio e centro del box di testo
(unità modello). `r = |P-C|` deve essere > 0; input non finiti sono rifiutati.
Il diametro misura `2r`, disegna il segmento `C - r*u .. C + r*u` e usa
`arrow_left`/`arrow_right` (default `CLOSED`) sugli estremi con le gambe verso
il centro. Il raggio misura `r`, disegna `C .. P` e usa una sola `arrow`
(default `CLOSED`) con punta su `P` e gambe verso il centro. `texts` contiene
prefisso e suffisso: default `Ø`/`""` e `R`/`""`; `text_height_mm` default
3,5 mm, `precision` 0..12, `pick_text` e stile
`pattern`/`weight`/`layer`/`color`; `scale` è in sola lettura. Il testo segue
la linea, resta dritto e viene centrato sul punto `[2]`; il generatore ricava
l'origine baseline-left dalla larghezza reale. Il diametro aggiunge un leader
dal centro del testo alla proiezione clampata sulla linea quando il testo non
è già sulla linea; il raggio aggiunge un leader da `P` all'ancoraggio quando
il testo non è sul segmento `C-P`. Il pick è linea + leader + frecce + OBB del
testo se `pick_text`. I wrapper CAD sono `cad_add_diameter_symbol()` e
`cad_add_radius_symbol()`: applicano il layer e la scala del documento e
creano il simbolo con l'API generica, registrando il solo `isogl_symbol_id`.

**Riga di tabella (`table_row`).** Origine in basso a sinistra in `points`
(unità modello), `height_mm` di carta, N celle con `widths` (mm di carta),
`alignments` (0 Sinistra, 1 Centro, 2 Destra) e `texts`, più
`text_height_mm`, `padding_mm`, `pick_text` e stile
`pattern`/`weight`/`layer`/`color`; `scale` è in sola lettura. Il generatore
puro converte le misure di carta in unità modello con la scala foglio, emette
bordo inferiore/superiore, N+1 separatori verticali e un run di testo per
cella con baseline centrata e allineamento orizzontale basato sulla larghezza
reale. `widths.count == alignments.count == texts.count` è obbligatorio; ogni
larghezza è > 0. Le tabelle enum ISO condivise sono in
`src/isogl_symbol_enums.*` e la metrica pura del font in
`src/isogl_font_data.*`; entrambi sono header/sorgenti interni, non installati.

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
(la modifica oggi è solo da console); transazione globale degli array
condivisi; preservazione dei tipi sconosciuti nel writer; caricamento di un
disegno completo nell’app. Le liste sono dinamiche (`NUMBERS`, `ENUM_LIST`);
`table_row` le usa, mentre `dimension` e `center_mark` restano a lunghezza
fissa min=max.

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
- `tests/test_symbol_geometry.c`: geometria pura headless (quota, center mark,
  diametro e raggio);
- `tests/test_table_row_geometry.c`: table_row puro headless con metrica stub;
- `tests/test_table_row.c`: registry, liste dinamiche, console, writer v2 e
  round-trip del table_row;
- `tests/test_diameter_radius.c`: diametro/raggio, wrapper CAD, bounds/hit/pick,
  params_update, console, writer v2 e round-trip (GL).

```sh
make test-cad-geometry      # archi/ellisse, generatore puro, headless
make test-cad-entities      # VBO, pick e selezione di archi/ellissi, GL
make test-symbol-geometry   # geometria pura, headless
make test-table-row-geometry # geometria pura table_row, headless
make test-console-cmd       # parser console, headless
make test-cad-symbols       # CadSymbolRef, update/remove/writer, round-trip, GL
make test-table-row         # table_row, liste dinamiche, console, round-trip, GL
make test-diameter-radius   # diametro/raggio, console, writer v2, round-trip, GL
make test-symbols
make test
```

## Archi ed ellissi

Archi ed ellissi sono **entità CAD** (non simboli isogl) con modello
analitico in RAM e VBO derivato. Il generatore puro è
`apps/isocad/src/cad_geometry.{c,h}`: niente GL/SDL e nessun accesso a
`CadDocument`.

Costruttori pubblici (`cad.h`):

```c
uint32_t cad_add_arc_center_angles(doc, layer, pattern, weight,
                                   cx, cy, radius, angle1, angle2);
uint32_t cad_add_arc_three_point(doc, layer, pattern, weight,
                                 sx, sy, ex, ey, cx, cy);
uint32_t cad_add_ellipse(doc, layer, pattern, weight,
                         p1x, p1y, p2x, p2y, p3x, p3y);
```

- **Arco da centro e angoli**: `radius > 0`; `a0 = angle1`; `sweep` è il delta
  antiorario da `angle1` a `angle2`, con wrap in `(0, 2π]`. Un sweep nullo o
  ambiguo (stessa semiretta, cioè 0 o 2π) è rifiutato. Per esempio
  350° → 10° dà 20°.
- **Arco da tre punti**: `radius = |start − centre|`; la fine deve stare sullo
  stesso cerchio entro `1e-4 + 1e-3 · radius` (unità modello), altrimenti
  l'entità è rifiutata. Il verso è antiorario (regola della mano destra attorno
  a +Z), quindi start (10,0) → end (0,−10) attorno all'origine è un arco di
  270°, non −90°.
- **Ellisse**: `p1`–`p2` è l'asse maggiore (`rx = |p2−p1|/2`, centro = punto
  medio, rotazione = `atan2(p2−p1)`); `ry` è la distanza perpendicolare di
  `p3` dalla retta `p1`–`p2`. Assi degeneri e valori non finiti sono rifiutati.

Tolleranza di corda: `CAD_CHORD_TOLERANCE_MM = 0.03` mm di carta, convertita in
unità modello con `tolerance / doc->sheet_scale`. È indipendente dallo zoom:
il VBO vive in mm di carta, quindi pan/zoom non rigenerano nulla. Il cap di
sicurezza è `CAD_TESSELLATION_MAX_SEGMENTS`. Per l'ellisse il conteggio parte
dal semiasse maggiore e viene raffinato verificando numericamente la massima
sagitta; il conteggio basato solo sul raggio di curvatura minimo
sottocampionerebbe i vertici del semiasse maggiore. L'ultimo punto dell'ellisse
e del cerchio coincide esattamente col primo.

Lo staging della tessellazione vive il tempo della chiamata: viene caricato nel
VBO e liberato subito. In caso di errore il costruttore restituisce 0 e non
lascia entità parziali nel documento (l'ID consumato non viene riusato).

La **selezione rettangolare** di archi ed ellissi non legge i VBO: riesegue il
generatore puro alla tolleranza di stampa e testa i segmenti campionati.
Window = tutti i punti campionati dentro il rettangolo; crossing = almeno un
segmento interseca, oppure curva contenuta. Layer e visibilità sono filtrati
come per le altre entità.

## Modello del documento

- `CadLayer`: nome, colore di stampa, peso e tipo di linea ISO predefiniti.
- `CadEntity`: linea, cerchio, arco, ellisse, rettangolo o testo, con layer,
  pattern e peso. Arco ed ellisse conservano centro/raggio/angoli e
  centro/semiassi/rotazione; la selezione usa questi parametri, non il VBO.
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

Milestone A + UI screen-space: foglio, linee, cerchi, archi, ellissi,
rettangoli, testo vettoriale, pattern e pesi ISO, zoom/pan, modalità
colori-spessore/stampa, selezione d’area persistente taggata CAD/simbolo, quota,
center mark e riga di tabella come simboli isogl con registry,
hit/pick/explode, record CAD con tipo+copia parametri, liste dinamiche
(`NUMBERS`, `ENUM_LIST`), modifica da console
(`sym list/info/set/points/remove`, `save`), ambiente di test gerarchico e
testo incrementale per entità. Archi ed
ellissi usano il generatore puro a tolleranza di corda; niente più 96 segmenti
fissi per il cerchio. L’interfaccia usa un overlay a gruppi
(`isogl_overlay`) con pannelli parametrici (`isogl_panel`): pannelli, icone come
gruppi di linee locali, hover con tooltip e console come gruppo testo; 6–8 draw
call UI. I pannelli hanno **quattro workspace** (F2–F5): la pagina attiva sta
nel dock, le altre restano retained fuori campo e nascoste. Editing, azioni dei
pulsanti, polilinee, export/stampa e modello CAD gerarchico sono i passi
successivi.
