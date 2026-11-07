# Contesto per agenti — isocad / isogl

Questo file è il contesto di progetto per chi lavora al repository. Va letto
prima di modificare il modello del documento, il rendering o la selezione.
Il documento di progetto completo, con sei diagrammi, è in
[`docs/generatore-di-simboli.html`](docs/generatore-di-simboli.html): aprilo
in un browser. Questo Markdown ne conserva le decisioni e le parti operative.

## Cos'è il repository

- `libisogl`: renderer OpenGL 2D per disegno tecnico, conforme a ISO 128
  (linee) e ISO 3098 (testo). Non è una libreria neutra: espone e impone le
  norme. Le tabelle normative stanno in `include/isogl/isogl_style.h`.
- `isogl_overlay` / `isogl_panel`: overlay screen-space a gruppi (quad, linee,
  testo) e layout puro dei pannelli. Sono **stato UI**, non documento, e non
  entrano nel modello CAD né nella tessellazione delle entità.
- `apps/isocad`: CAD 2D minimale costruito sopra la libreria. Possiede
  finestra, contesto, documento (layer ed entità) e comandi.
- C11, OpenGL 3.3 core, glad, tomlc99. **SDL2 non è una dipendenza del
  core**: compare solo nel pacchetto opzionale `hosts/isogl-sdl`, nella demo e
  nei test GL che hanno bisogno di un contesto. Nessuna dipendenza da FreeType
  o Pango: i font sono vettoriali e caricati da TOML.
- Contratto: l'applicazione crea e distrugge contesto e finestra, possiede lo
  swap e rende corrente il contesto. `gladLoadGLLoader()` prima di
  `isogl_init*`; un solo contesto corrente; nessuna thread-safety;
  `isogl_cleanup()` idempotente.
- Viewport: l'ospite chiama `isogl_set_viewport()` con un rettangolo in pixel
  di framebuffer (origine GL, basso-sinistra) e `isogl_begin_frame()` dopo il
  lavoro GL del toolkit. Tutte le altre coordinate in pixel sono **locali al
  viewport, origine in alto a sinistra**. `isogl_pick_at()` prende anche
  `extent_px`, in pixel di framebuffer; gli host HiDPI passano
  `lround(ISOGL_PICK_DEFAULT_EXTENT * dpr)`.
- Host verificati: SDL2 (`hosts/isogl-sdl`), GTK4 `GtkGLArea`
  (`examples/gtk4`), Qt6 `QOpenGLWidget` (`examples/qt6`). GTK4 e Qt6 usano i
  loro contesti EGL nativi: Wayland funziona senza `SDL_CreateWindowFrom`.

Stato: Milestone A + UI screen-space. Esistono foglio, linee, cerchi,
rettangoli, testo, pattern e pesi ISO, zoom/pan, modalità colori-spessore,
selezione d’area persistente e i primi simboli isogl (quota, center mark) con
registry, hit/pick/explode. L’app usa già riferimenti simbolo al posto della
vecchia `CAD_DIMENSION` e ID entità monotoni.

L’interfaccia di isocad usa un **overlay screen-space a gruppi**:
`isogl_overlay` batcha quad, linee e testo in un unico pass, con
trasformazione/layer/clip/colore per gruppo e dirty per gruppo; si aggiorna su
hover, input di testo, resize/DPR, cambio icone e tooltip, mentre i frame
statici non fanno upload. `isogl_panel` fornisce il layout puro e parametrico
dei pannelli (spessore per bordo, separatori, content rect). L’app compone
pannelli, icone (un gruppo di linee locali per icona), hover, console e tooltip
da gruppi; il costo UI è di 6–8 draw call. L’overlay è stato UI, non documento.

Editing, azioni dei pulsanti, archi, polilinee, export e modello CAD gerarchico
non sono implementati.

Il pick GPU è ora implementato in `isogl_pick_at` (`GL_R32UI`, box logico 8×8
pixel, texture 16×16 per 2× di definizione, stesso VS/GS e `fs_pick.glsl` per
i segmenti, `fs_font_pick.glsl` per il testo), con test in `tests/test_core.c`.
Il readback è scansionato a spirale dal centro verso l’esterno, quindi gli ID
distinti sono ordinati dal più vicino al cursore; `out_ids[0]` è il candidato
centrale. Mancano il pre-filtro di distanza e la politica topmost/centro. Il
pick pass disegna tutti gli oggetti con `pick_id != 0`, non solo i candidati.

La selezione d’area è implementata in `isocad`: drag sinistro con start in mm
e modo dinamico (sinistra→destra window, destra→sinistra crossing). Durante il
drag la CPU ricalcola il set esatto con `cad_select_rect()` a ogni movimento e
lo applica al renderer come flag per entità (`cad_selection_apply`):
`isogl_vbo_object.selected` e `isogl_glyph_string.selected`, tinti dal FS via
`uSelected`. La selezione è taggata: entità CAD e simboli isogl hanno namespace
distinti; per i simboli il rettangolo usa `isogl_symbol_hit` e il tint
`isogl_symbol_set_selected`, senza leggere i loro VBO. Il set resta evidenziato
dopo il rilascio e nei frame successivi,
anche dopo pan/zoom; è stato UI, non documento, e va riapplicato per ID dopo un
rebuild. Il rettangolo (`vs_selection.glsl`/`fs_selection.glsl`) è solo overlay
rubber-band. Il predicato per segmento nel GS
(`uSelActive/uSelCrossing/uSelRectNdc`) resta disponibile come anteprima veloce
ma è disattivato in isocad (`isogl_set_selection_preview(0)`), così anteprima e
set finale coincidono; il testo usa il flag per stringa, non più il test per
frammento. Il pan è sul tasto centrale e non interrompe la selezione; la
rotella zooma anche durante il drag; Esc annulla e svuota il set, Q esce.
Mancano il pre-filtro del click e la politica topmost/centro; l’OBB del testo
per la selezione a rettangolo è calcolato dagli advance reali
(`isogl_font_text_advance`), non più da `strlen * 0.6 * h`.

## Decisione architetturale da rispettare

Il generatore di simboli è implementato per i **simboli isogl** (quota,
center mark): geometria pura in `isogl_symbol_geometry.*`, registry GL in
`isogl_symbol.*`. Il modello CAD gerarchico (Drawing → Group → Primitive) e il
poligono regolare CAD-side restano la direzione successiva. Il punto centrale è
separare due consumatori del documento, invece di metterli in catena.

```
                         ┌──────────────────┐
                         │   CadDocument    │
                         │ modello analitico│
                         │      (RAM)       │
                         └───────┬──────────┘
                                 │ parametri
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
      ┌────────────────────┐          ┌────────────────────┐
      │  Kernel geometrico │          │ Generatore simboli │
      │ bounds / hit_test  │          │  entità → polilinee│
      │ snap / intersezioni│          │  + run di testo    │
      │ puro, nessun GL    │          │  puro, nessun GL   │
      └─────────┬──────────┘          └─────────┬──────────┘
                │ id entità                     │ staging transitorio
                ▼                               ▼
      ┌────────────────────┐          ┌────────────────────┐
      │ Selezione / comandi│          │  isogl → VBO/VRAM  │
      └────────────────────┘          └────────────────────┘
```

Regole che ne derivano:

1. **La selezione non legge i segmenti.** Usa i parametri analitici
   dell'entità. Il collegamento selezione → staging/VRAM non deve esistere.
2. **Il modello analitico sta in RAM perché è il documento.** Serve per
   editing, undo, snap, salvataggio, export e rigenerazione.
3. **Non esiste una copia RAM permanente dei segmenti.** Lo staging
   (`isogl_segment_list`) vive il tempo di un upload e viene liberato subito;
   i segmenti definitivi stanno solo in VRAM.
4. **La CPU fa selezione, snap e tessellazione una tantum. La GPU rasterizza,
   tratteggia, clippa e mappa i colori-spessore.**
5. **Il generatore è puro:** nessuna chiamata GL o SDL, nessuna dipendenza dal
   documento. Riceve entità + stile, emette polilinee e run di testo tramite
   callback.

## Invarianti e trappole

- **Niente mirror della tessellazione.** Un cerchio a 96 segmenti costa
  192 vertici × 32 B = 6 KiB. Diecimila cerchi = ~60 MiB. Il modello analitico
  degli stessi cerchi è circa 1 MB. Duplicare i segmenti in RAM raddoppia il
  dato più grande senza aggiungere capacità. L'**overlay UI** è l'eccezione
  documentata: le sue liste CPU sono stato UI, non geometria del documento.
- **Tessellazione a tolleranza di corda, non a conteggio fisso.**
  `N = ceil(π / acos(1 − s/r))`. La tolleranza è in mm di carta (0,02–0,05 mm,
  sotto il peso minimo ISO di 0,18 mm) e **non** dipende dallo zoom. Con
  `s = 0,05 mm`: r = 5 mm → N ≈ 23; r = 50 mm → N ≈ 71; r = 500 mm → N ≈ 223.
  Il 96 fisso attuale è sbagliato sia per i raggi piccoli sia per i grandi.
- **Stile per polilinea, non per entità.** In una quota la linea di quota e i
  riferimenti seguono il layer, ma punte e testo hanno vincoli propri. Oggi
  `cad_vertex()` prende pattern e peso dall'entità per tutti i vertici.
- **Il testo è un run separato**, non un insieme di segmenti: va alla pipeline
  font. Ogni stringa ha un **VBO completo** di coppie `GL_LINES` (glifi
  duplicati, nessun atlas/instancing), disegnato con una `glDrawArrays` per
  stringa. L’update è per `pick_id`: contenuto → nuovo VBO della stringa;
  posizione/angolo/altezza → solo `string_matrix`. L'emitter ha due callback,
  `polyline` e `text`.
- **Aggiornamento per entità sporca.** Una versione per entità, un dirty set,
  un emit per entità. Se il conteggio vertici non cambia: `glBufferSubData`
  nel range. Se cambia: nuovo range da free-list o rebuild della pagina.
- **Vista ≠ modello.** Pan e zoom cambiano solo `uView`/`uModel`: layout e VBO
  sono in mm di carta, quindi pan/zoom non rigenerano nulla e non toccano la
  VRAM. Traslare una *entità* riscrive solo il suo range; a parità di conteggio
  vertici è una `glBufferSubData`, non un rebuild del disegno. La tolleranza di
  corda è di stampa, non di schermo, quindi nemmeno lo zoom richiede una nuova
  tessellazione.
- **Il batch non è per stile e non è un oggetto.** Pattern, peso, layer e
  colore sono attributi per vertice: una pagina può contenere stili misti.
  L'identità dell'entità è il range `entity_id → (page, first, count)`; la
  selezione non usa il range. Un draw per pagina basta; `glMultiDrawArrays`
  serve solo a saltare range invisibili. Il generatore non decide il VBO:
  emette verso un emitter, il backend sceglie se usare un VBO per entità o una
  pagina.
- **Handle stabili.** L'entità non deve conoscere il VBO; il backend tiene
  `entity_id → range/slot`. `isogl_vbo_array_remove()` oggi shifta l'array e
  non ha free-list: non è una base stabile per undo e selezione.
- **Pick GPU solo per click/hover; rettangolo analitico.** Il click usa un FBO
  di ID 16×16 (`GL_R32UI`, 4 B/pixel) su un box logico 8×8: seleziona ciò che
  è colorato a schermo.
  Il rettangolo resta analitico: il pixel buffer non risponde a “contenuto” e
  una linea tratteggiata può attraversarlo senza pixel dentro.
- **L'AABB non basta per il click.** A zoom alto è troppo conservativo: il box
  di un cerchio include l'interno, quello di una diagonale è enorme. Dopo la
  broad phase AABB/grid, filtra con la distanza analitica dal cursore alle
  primitive del layout, tolleranza `pick_radius_mm + sagitta`. Il filtro è solo
  un early-out: il pick pass GPU decide sul pixel.
- **Il pick pass è fedele al display.** Stessi VBO, stesso GS/FS con
  `uPickId`/`uPickMode`, stessi filtri di visibilità/layer/clipping; niente
  blending, MSAA o `GL_LINE_SMOOTH`. I vuoti del tratteggio sono ignorabili
  (`pick_ignore_dashes`): il tratteggio è stile, non selezionabilità. Gli altri
  vuoti (layer nascosto, interno del cerchio) restano non selezionabili.
- **ID a 32 bit, non il colore di visualizzazione.** `CadEntity.id` è
  `uint32_t` (4 B); il nome del VBO è `GLuint` (4 B) ma non è stabile. L'ID di
  pick è l'ID entità, passato come uniform per draw: con i range candidati non
  serve un attributo per vertice. 0 = nessuna entità.
- **Soglia configurabile.** `pick_radius_px` e `pick_max_entities` sono
  parametri. Dopo il readback conta gli ID distinti nel 16×16, scansionato a
  spirale dal centro: 0 = niente,
  1 = seleziona, 2..max = centro/più vicino/menu, > max = ambiguo.

## Modello del documento

- `CadLayer`: nome, colore di stampa, peso e tipo di linea ISO predefiniti.
- `CadEntity`: linea, cerchio, rettangolo o testo; layer, pattern, peso,
  visibilità, `vbo_id`.
- `CadDocument`: array di entità con `next_id` monotono, riferimenti ai simboli
  isogl (`isogl_symbol_id *symbols`), specchio GPU (`isogl_vbo_array` per i
  segmenti, `isogl_string_list` per il testo), font, scala foglio.
- Ogni entità geometrica ha oggi il proprio VBO. La modifica di un'entità
  ricostruisce solo la sua lista di segmenti e chiama
  `isogl_vbo_array_update()`; il resto resta in VRAM.
- Il testo è incrementale per entità: `cad_add_text` crea solo la sua stringa;
  `cad_update_text`/`cad_set_text_transform` aggiornano una sola voce per
  `pick_id` (VBO nuovo solo se cambia il contenuto, matrice se cambia solo la
  posizione). `cad_document_rebuild_text` resta per ricarica font/rebuild
  globale esplicito. Ogni stringa ha un VBO completo di coppie `GL_LINES`,
  senza `glMultiDrawArrays`. Memoria: font unico ~50–60 KiB; stringa tipica
  ~0,5–0,8 KiB; 10.000 stringhe ~6–8 MB di VBO; a scala il limite sono i VBO e
  le draw call, non i byte.

La quota è il caso guida: in memoria `p1` e `p2` (punti misurati), `p3`
(punto direzione/lato), `distance` (offset della linea di quota in unità
documento; 0 = ricavato da `p3`), `text_offset_mm` (posizione del testo
rispetto al centro, in mm di carta e con segno lungo la linea),
`text_left`/`text_right` (testo libero prima/dopo il valore) e
`arrow_left`/`arrow_right` (`NONE`, `OPEN`, `CLOSED`, `TICK`, `DOT`,
`OPEN_OUTSIDE`, `CLOSED_OUTSIDE`) e `mark_points`. Proporzioni di default:
testo **2,5 mm**, frecce **2,2 mm** con ampiezza **30°**, pallino **1,5 mm**;
le linee di riferimento partono **3 mm** dopo il punto misurato. Le varianti
`*_OUTSIDE` hanno la punta sull’estremo e le gambe fuori dal tratto, quindi
puntano all’interno; la linea di quota viene estesa di **5 mm** oltre
quell’estremo per ospitare la freccia. `mark_points` aggiunge un segmento di
riferimento da **0,5 mm** che parte da `p1`/`p2` verso la linea di quota, prima
del distacco di 3 mm (circa un pixel alla scala di stampa; la pipeline a linee
non disegna punti singoli). Il valore non è memorizzato: si calcola da
`|p1 − p2|` e **non ha unità automatica**; chi vuole “ mm” lo mette in
`text_right`. Le coordinate del modello sono in unità
documento; la scala del foglio governa le grandezze di carta (altezza testo,
frecce, tolleranze), non il valore misurato. Il generatore produce linee di
riferimento, linea di quota, frecce secondo il tipo scelto e un run di testo.

La prima entità reale è la quota allineata, ora **simbolo isogl**. Il CAD
costruisce ogni volta un `IsoglDimensionDesc` completo (`p1`, `p2`, `p3`,
`distance`, `text_offset_mm`, `text_left`/`text_right`, `arrow_left`/`right`,
precisione, altezza testo, `pick_text`), chiama `cad_add_dimension_symbol` e
conserva solo l’`isogl_symbol_id`. La geometria pura
(`isogl_symbol_geometry`) deriva render e pick; il registry (`isogl_symbol`)
possiede VBO, stringa, hit/pick e explode. La vecchia `CAD_DIMENSION` è stata
rimossa dal modello CAD. Il valore si formatta con la precisione richiesta;
`pick_text = 0` esclude l’OBB del testo dal rettangolo lasciandolo cliccabile
via pick GPU.

## Simboli isogl e confine con il CAD

Due domini separati:

- **geometria CAD** (oggi flat, futuro Drawing → Group → Primitive): il CAD
  possiede parametri, gruppi e trasformazioni; l'adapter di rendering produce un
  VBO di segmenti; isogl non conosce il significato. Il poligono regolare sarà
  CAD-side, con generatore ed explode del CAD.
- **simboli isogl** (quota, center mark): isogl possiede record e geometria. Il
  CAD conserva solo un `isogl_symbol_id` opaco; usa
  `isogl_symbol_hit/bounds/pick` e, per modificare, richiama
  `isogl_symbol_update_dimension`/`update_center_mark` con un descrittore
  completo; `isogl_symbol_explode` trasferisce segmenti e testo al CAD e
  distrugge il simbolo. Nessuna lettura dei VBO interni del simbolo.

Implementazione:

- `include/isogl/isogl_symbol_geometry.h`, `src/isogl_symbol_geometry.c`:
  geometria pura senza GL/SDL per quota e center mark, emitter render e pick,
  metrica della larghezza testo fornita dal chiamante.
- `include/isogl/isogl_symbol.h`, `src/isogl_symbol.c`: registry GL con ID
  monotoni non riusati, VBO/stringhe posseduti dal modulo, draw/pick/hit/
  bounds/selected e explode transazionale (fallimento dell'emitter = simbolo
  vivo). `isogl_string_list_remove_by_pick_id` libera la stringa del simbolo;
  `isogl_symbol_update_dimension`/`isogl_symbol_update_center_mark`
  rigenerano un simbolo **in place**, preservando ID, pick e selezione.
  `isogl_symbol_params_resize` ridimensiona le liste (punti/testi/segmenti)
  entro i limiti dello schema, per il loader e per l'editing.
  La scala foglio è un globale in sola lettura; il percorso di update è
  atomico per simbolo (nuovo VBO e nuove stringhe prima di rimuovere i
  vecchi).
- File drawing schema v2 (`examples/demo/drawing_load.*`): `[[symbols]]` con
  `type` + `group`, `[symbols.params]` con i valori per chiave dello schema
  registrato; `DrawingSymbolEntry`/`DrawingSymbols` possiedono `type` e
  `isogl_symbol_params` (`drawing_symbols_free`). Il loader applica le
  trasformazioni di gruppo a punti e segmenti (le misure di carta non si
  scalano), valida nomi/conteggi contro il tipo C, usa i default per le chiavi
  assenti e traduce lo schema piatto storico (`p1x/p2x/p3x`, `cx/cy`). SCALE e
  `sheet_scale` non si salvano per simbolo: la scala è globale in `[sheet]`.
- Tipo sconosciuto: l'entry resta con `params == NULL`, viene saltata con un
  warning e non contribuisce ai bounds; il loader non inventa geometria. Il
  writer `drawing_write_symbols`/`drawing_save_symbols` riscrive
  `[[symbols]]` + `[symbols.params]` omettendo SCALE e i blocchi sconosciuti
  (nessuna rappresentazione grezza: non si promette preservazione).
- Ordine della demo: `isogl_symbol_init(font)` prima di `drawing_load`, poi
  `isogl_symbol_set_sheet_scale(scale_num/scale_den)` e
  `isogl_symbol_create(type, params, scale)` per ogni entry nota. Il loader
  non crea simboli.
- Test: `tests/fixtures/test_drawing_v2.toml` (dimensione, center mark, tipo
  sconosciuto) e `tests/test_drawing_load.c` coprono params, trasformazione,
  skip, bounds, creazione, pick e round-trip writer→loader; la vecchia fixture
  resta per la compatibilità p1x.
- L'app `isocad` usa riferimenti simbolo: `CadDocument` conserva
  `isogl_symbol_id *symbols`; `cad_add_dimension_symbol` /
  `cad_add_center_mark_symbol` creano il simbolo e ne registrano l’ID. La
  selezione è taggata (`CAD_SEL_ENTITY` / `CAD_SEL_SYMBOL`): le entità CAD
  usano `cad_selection_apply`, i simboli `isogl_symbol_set_selected`, e il
  rettangolo interroga i simboli con `isogl_symbol_hit`. La demo mostra quota e
  center mark come simboli.
- L'ambiente di verifica è `tests/fake_cad.*` (fake CAD gerarchico, non il CAD
  di produzione) + il disegno strutturato
  `tests/fixtures/test_drawing.toml`; `tests/test_symbols.c` verifica load,
  render, pick, hit, selezione, explode e trasferimento al CAD.
  `tests/test_symbol_geometry.c` è headless.

### Contratto reale e lacune

**Ownership e copia.** isogl possiede record, **copia** del descrittore, VBO e
glyph string. Il CAD possiede solo l'`isogl_symbol_id` opaco. Modificare il
descrittore del chiamante dopo la creazione non cambia il simbolo: serve un
`update_*` con il descrittore completo. Gli ID sono monotoni, non riusati; un ID
distrutto resta morto (tombstone).

**API attuale.**

| Funzione | Contratto |
|---|---|
| `isogl_symbol_init(font)` / `cleanup()` | registry GL; il font deve sopravvivere; cleanup prima di `isogl_cleanup()` |
| `isogl_symbol_register_type(type)` / `find_type` / `type_count` / `type_at` | registry pubblico dei tipi C; nomi duplicati rifiutati; `dimension`/`center_mark` registrati all'init |
| `isogl_symbol_set/get_sheet_scale(scale)` | scala foglio **globale** in sola lettura dai parametri; default 1.0; scarta valori non finiti o ≤ 0; `set` rigenera **tutti** i simboli vivi, conservando ID, pick e selezione |
| `isogl_symbol_create(type_name, params, scale)` | crea un'istanza di un tipo C registrato; copia i parametri; genera render+pick. L'argomento `scale` resta solo per compatibilità ABI: vale la scala globale |
| `isogl_symbol_params_create/free` | alloca/libera un set di parametri conforme allo schema del tipo |
| `isogl_symbol_params_get(id, &out)` | copia profonda dei parametri correnti; il parametro `SCALE` riporta la scala applicata al simbolo (`editable = 0`) |
| `isogl_symbol_params_update(id, params)` | rigenera in place da parametri tipizzati; conserva ID, pick e selezione; **atomico per simbolo**; ignora il valore `SCALE` in ingresso e usa quello globale |
| `isogl_symbol_params_set_text(...)` | sostituisce un elemento testo con copia UTF-8 |
| `isogl_symbol_create_dimension/center_mark` | wrapper compatibili che costruiscono i parametri generici |
| `isogl_symbol_update_dimension/center_mark` | wrapper compatibili; la scala è quella globale, non il campo del descrittore |
| `isogl_symbol_destroy(id)` | rimuove VBO/stringhe e invalida l'ID |
| `isogl_symbol_draw_all()` | disegna tutti i simboli vivi; non esiste un filtro di visibilità per simbolo |
| `isogl_symbol_pick_at(px,py,extent,...)` | pick GPU sul registry; ritorna gli ID simbolo; non legge `CadDocument.symbols` |
| `isogl_symbol_hit/bounds` | geometria analitica di **pick**; con `pick_text = 0` il testo è escluso, quindi bounds è più piccolo del render |
| `isogl_symbol_set/get_selected` | flag persistente su VBO e stringhe |
| `isogl_symbol_explode(id, out)` | riemette geometria render e distrugge il simbolo; se l'emitter fallisce il simbolo resta vivo. Gli item emessi non hanno ID CAD |

Il tipo C dichiara i parametri e le descrizioni per l'editor:

```c
typedef struct {
    const char *key, *label, *help, *unit;
    isogl_symbol_param_kind kind;
    size_t min_count, max_count;
    int editable;
} isogl_symbol_param_desc;

typedef struct isogl_symbol_type {
    const char *name, *label, *description;
    const isogl_symbol_param_desc *params;
    size_t param_count;
    int (*init_params)(isogl_symbol_params *);
    int (*validate)(const isogl_symbol_params *);
    int (*generate)(const isogl_symbol_params *,
                    const isogl_symbol_gen_context *,
                    IsoglSymEmitter *, IsoglSymPickEmitter *);
} isogl_symbol_type;
```

I parametri sono liste tipizzate (`POINTS`, `TEXTS`, `SEGMENTS`), scalari
(`NUMBER`, `INTEGER`, `BOOLEAN`, `ENUM`, `COLOR`) e la `SCALE` di contesto in
sola lettura. Dimension e center mark sono adapter sui generatori puri
esistenti; un tipo custom è nuovo codice C registrato dall'app.

**Scala foglio globale.** `isogl_symbol_set_sheet_scale()` è l'unica autorità:
tutti i simboli vivi usano quel valore, anche quelli creati con un argomento
diverso (mantenuto per ABI). `params_get` riporta la scala applicata al
simbolo nel parametro `SCALE`; `params_update` ignora il valore `SCALE`
in ingresso. Se un simbolo non riesce a rigenerarsi durante il cambio globale,
conserva geometria e scala precedenti e `set_sheet_scale` continua con gli
altri, restituendo `-1` se almeno uno ha fallito (il globale resta comunque il
nuovo valore).

**Aggiornamento atomico per simbolo.** `params_update` e la rigenerazione di
scala costruiscono prima i nuovi segmenti, tutte le glyph string e la copia dei
parametri; poi creano un **nuovo VBO** e accodano le stringhe nuove con
`pick_id` temporaneo 0. Solo a quel punto rimuovono le vecchie stringhe e il
vecchio VBO, promuovono le nuove al `pick_id` reale, ripristinano la selezione
e sostituiscono parametri e pick item. Un fallimento in validazione,
generazione, allocazione o creazione stringhe lascia intatti VBO, stringhe,
pick item, parametri e selezione precedenti. Limite residuo: gli array
condivisi (`symbol_parts`, `symbol_strings`) non offrono una transazione
globale; l'atomicità è per singolo simbolo e un cambio globale può quindi
riuscire per alcuni simboli e fallire per altri.

**Regola CAD.** Il CAD può leggere i metadati del tipo e i valori correnti con
`params_get`/`type_at`, e modificare con `params_update`. Non può leggere VBO,
glyph string o segmenti interni; l'ID resta l'handle per pick/selezione/explode.
La scala è globale e di sola lettura: si cambia solo con
`isogl_symbol_set_sheet_scale`.

**Lacune e passi successivi.** Restano: file v2 con `type` +
`[symbols.params]` e skip/preservazione dei tipi sconosciuti (slice 3);
pannello proprietà e `CadSymbolRef` in isocad; ridimensionamento dinamico delle
liste (in v1 i tipi built-in hanno lunghezze fisse min=max); `isocad` conserva
ancora solo gli ID, non usa `params_get` e non chiama ancora
`isogl_symbol_set_sheet_scale` (continua a passare `doc->sheet_scale` ai
wrapper, che ora è ignorato); transazione globale degli array condivisi.

**Direzione CAD.** `CadSymbolRef` conterrà il nome del tipo e una copia di
`isogl_symbol_params` (o li interrogherà con `params_get`) per salvataggio,
undo e pannello proprietà; `cad_update_*_symbol` chiamerà `params_update`,
`cad_remove_symbol`/`cad_explode_symbol` aggiorneranno il documento senza
lasciare ID morti.

## Overlay e pannelli

L'interfaccia screen-space non usa scissor+clear né uno shader privato dell'app.
`isogl_overlay` (`include/isogl/isogl_overlay.h`, `src/isogl_overlay.c`) espone
**gruppi di primitive** in pixel di framebuffer, viewport-local, origine in alto
a sinistra:

- ogni gruppo ha quad, linee e testo in coordinate locali, più trasformazione
  `(tx, ty, scale)`, layer, clip rect, visibilità e override di colore;
- ogni mutazione marca solo quel gruppo dirty; `isogl_overlay_commit()`
  ricostruisce gli staging condivisi e carica i VBO solo quando serve. Nei frame
  statici non c'è upload;
- `isogl_overlay_draw()` usa una proiezione pixel propria e **non modifica**
  `isogl_model_matrix`/`isogl_view_matrix`; il clip è per-vertice su quad/linee e
  per-stringa sul testo, quindi non moltiplica le draw call;
- l'ordine è per layer e poi per creazione: in ogni layer quad, linee, testo.
  Un tooltip è un gruppo in un layer alto con un quad e un testo, non un tipo
  speciale.

Trigger di aggiornamento: hover, input di testo, resize/DPR, cambio icone
(`group_clear` + re-add), visibilità/layer/clip, movimento del tooltip. Le liste
CPU retained sono **stato UI**, non geometria del documento: l'eccezione al
divieto di copia RAM dei segmenti vale solo per l'overlay.

I pannelli sono organizzati in **quattro workspace globali** selezionati da
F2–F5: la pagina attiva sta nel dock, le altre restano gruppi retained
trasformati fuori campo e con `visible = 0`. Il cambio è istantaneo (niente
slide in v1): l'overlay ricostruisce la geometria visibile per un frame, poi
torna al costo statico. `content`, fit e filtro input non cambiano; hover e
tooltip considerano solo la pagina attiva. `apps/isocad/src/panel_deck.c`
contiene solo la matematica pura di slot/parcheggio ed è testato headless;
`panels.c` possiede le pagine, le icone e il passaggio di pagina.

`isogl_panel*` è il layout puro dei pannelli (`top/left/right/bottom`,
`separator`, `content`) più l'emissione dei quad di sfondo/separatore. Il CAD usa
`content` per il fit della scena e gli inset per filtrare l'input, e disegna il
contenuto dei pannelli in pixel con le primitive dell'overlay. Le linee
dell'overlay sono `GL_LINES` a 1 px: **non esiste** un helper che le converta in
quad spessi.

Test: `make test-panel-layout` (headless), `make test-panel-deck` (headless),
`make test-overlay` (GL, FBO), `make test-drawing-load` (GL, loader gerarchico
e simboli), `make test-symbol-params` (GL, registry tipi C e parametri).
`apps/isocad/src/panels.c` compone i gruppi icona, le pagine e il
tooltip; `console.c` emette le righe nel gruppo testo.

Il testo dell'overlay è disegnato con
`isogl_draw_string_list_ex(..., apply_selection = 0)`: il rettangolo di
selezione della scena non tinge etichette, tooltip o console. La selezione
persistente resta un concetto del documento; l'overlay non la legge.

## Selezione

Due modalità, due rappresentazioni. Il kernel analitico non sparisce: cambia
ruolo.

### Click e hover: il pixel è la verità

Il rendering è una spezzata: a zoom alto la corda visibile non coincide con
l'arco analitico. Per il click la verità è il pixel colorato, non la curva
ideale. L'ID dell'entità è il ponte: il pick restituisce l'ID, l'editing usa il
modello double.

1. Filtra layer, visibilità e selezionabilità.
2. Broad phase AABB/grid sul rettangolo del cursore.
3. Pre-filtro analitico sulla distanza alle primitive del layout:
   segmento = punto-segmento; cerchio = `|dist(centro, cursore) − r|`;
   arco = distanza dall'arco con controllo angolare, altrimenti estremo;
   testo = OBB; quota = tutte le primitive visibili (il flag `pickable` del
   layout serve al rettangolo, non al click).
   Tolleranza = `pick_radius_mm + sagitta + epsilon`. Serve a scartare i falsi
   positivi dell'AABB (interno del cerchio, diagonali lunghe).
   Se i candidati AABB restano molti — una linea lunga ha un AABB grande —
   indicizza le primitive (R-tree o grid sui segmenti/archi), non solo le
   entità. Il test di distanza resta O(candidati) e avviene solo al click.
   A zoom basso la tolleranza in mm cresce e il box contiene oggetti che sono
   davvero vicini: il pre-filtro non li può separare. L'ambiguità si gestisce
   con `pick_max_entities` e un menu/cycle, non con il filtro.
4. Pick pass GPU sui soli candidati: FBO 16×16 `GL_R32UI` (box logico 8×8)
   con matrice di pick
   centrata sul cursore, clear 0, stessi VBO/GS/FS in modalità pick, un uniform
   `pick_id` per draw, niente blending/MSAA/smooth. Disegna tutte le primitive
   visibili: pre-filtro e pick pass devono vedere lo stesso insieme. Un
   eventuale flag “pickable per click” va applicato a entrambi. Se
   `pick_ignore_dashes`, il FS non scarta i vuoti del tratteggio.
   L'attachment è una texture allocata con
   `glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 5, 5, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL)`.
5. `glReadPixels(GL_RED_INTEGER, GL_UNSIGNED_INT)`, conta gli ID distinti:
   0 = niente; 1 = seleziona; 2..max = pixel centrale, più vicino o menu;
   > max = ambiguo.

Il pick pass non è il percorso caldo: può fare un draw per candidato. Con le
pagine VBO si disegnano i soli range candidati, senza aggiungere un attributo
ID per vertice. Anche il testo deve avere un `pick_id` (una quota ha segmenti e
testo con lo stesso ID entità). Per il pre-filtro del testo serve
`isogl_text_extents()`.

### Rettangolo: window e crossing

Dato un rettangolo:

1. Converti i due angoli in pixel con l'inversa di `uView · uModel` e
   normalizza min/max. Tutti i test avvengono in mm di carta.
2. Applica prima i filtri di layer e visibilità.
3. Broad phase: AABB analitico per entità, con un grid sul foglio.
4. Narrow phase analitica per tipo:
   - segmento: Liang–Barsky (lo stesso algoritmo del geometry shader);
   - polilinea: tutte/una delle coppie;
   - cerchio: `d_near ≤ r ≤ d_far` per attraversare il bordo; centro dentro e
     `r ≤ d_near` per essere contenuto. `d_near` è la distanza minima del
     centro dal bordo del rettangolo (se il centro è dentro, dal lato più
     vicino); `d_far` è la distanza massima dai quattro vertici;
   - arco: come il cerchio, più estremi e range angolare;
   - testo: OBB e SAT; serve l'estensione del testo;
   - quota: geometria di pick (linea di quota e riferimenti), non tutti i
     segmenti generati.
5. Semantica: **window** = solo contenute; **crossing** = contenute oppure
   attraversanti il bordo. Sono due predicati.

`isogl_font_text_advance()` calcola la larghezza dagli `advance` reali; l'OBB
del testo (entità testo e testo della quota) la usa nella selezione a
rettangolo. Manca ancora `isogl_text_extents()` come API completa per il
pre-filtro del click; gli `advance` sono in `isogl_font_data`.

## API proposta

```c
typedef struct {
    void *user;
    void (*polyline)(void *user, const float *xy, uint32_t count,
                     uint8_t pattern, uint8_t weight, uint8_t layer);
    void (*text)(void *user, const char *utf8, float x, float y,
                 float height, float angle, int align);
} symgen_emitter;

void symgen_emit(const CadEntity *e, const CadStyle *style,
                 symgen_emitter *out);

bool entity_bounds(const CadEntity *e, sym_box *out);
bool entity_hit(const CadEntity *e, const sym_box *query,
                sym_hit_mode mode);   /* CONTAINED | CROSSING */

typedef struct {
    GLuint vbo_id;          /* oppure la glyph string, per il testo */
    uint32_t entity_id;     /* l'ID opaco che torna dal pick */
} isogl_pick_item;

typedef struct {
    isogl_viewport viewport;      /* pixel di framebuffer, origine GL */
    float px, py;                 /* locali al viewport, top-left     */
    int extent_px;                /* lato del box, pixel fisici       */
    const isogl_pick_item *items;
    size_t count;
    uint32_t *out_ids;
    size_t out_capacity;
    size_t *out_count;
} isogl_pick_request;
```

L'emitter scrive direttamente nello staging: nessuna display list intermedia.
`isogl` resta il renderer e non conosce né il kernel né i simboli. Anche il
pick è entity-agnostic: l'app fornisce i candidati e gli ID; la libreria
possiede FBO e shader. `extent_px` è un parametro, non una costante: su HiDPI
l'host lo scala con il device pixel ratio. Il `pick_id` è opaco; il testo ne ha
uno.

## Punti del codice attuale

Riferimenti indicativi; i numeri di riga cambiano spesso, verificali prima di
fidarti.

| Punto | Oggi | Direzione |
|---|---|---|
| `include/isogl/isogl.h:59` | `isogl_vertex` 32 B | invariato |
| `apps/isocad/src/cad.c:8` | 96 segmenti fissi per cerchio | `tessellate_arc(r, tolleranza)` |
| `apps/isocad/src/cad.c:38` | stile per entità su tutti i vertici | stile per polilinea |
| `apps/isocad/src/cad.c:237` | rebuild globale del testo | testo incrementale per entità (fatto) |
| `src/isogl.c:629` | add di un VBO per entità | invariato finché non si impagina |
| `src/isogl.c:718` | update del singolo VBO | invariato; base per il delta |
| `src/isogl.c:842` | remove con shift dell'array | handle stabili + free-list |
| `src/isogl.c:954` | una draw call per entità | un draw per pagina; multi-draw solo per range invisibili |
| `src/isogl.c:1038` | draw di tutti gli oggetti | batching quando il profilo lo chiede |
| `apps/isocad/src/cad.h` | `CadEntity.id` `uint32_t` da `next_id` monotono | ID stabile per il pick (fatto) |
| `include/isogl/isogl.h` | `isogl_vbo_object` senza pick ID | campo `pick_id` (fatto) |
| `include/isogl/isogl_font.h` | glyph string senza pick ID | campo `pick_id` (fatto) |
| `shaders/fs_pick.glsl` | nuovo | fragment shader che scrive `uPickId` |
| `shaders/fs_font_pick.glsl` | nuovo | pick del testo |
| `src/isogl.c` `isogl_pick_at` | implementato con `extent_px` | mancano pre-filtro e topmost |
| `include/isogl/isogl_overlay.h` | overlay a gruppi (quad/linee/testo), dirty per gruppo | fatto |
| `include/isogl/isogl_panel.h` | layout pannelli puro + emitter | fatto |
| `apps/isocad/src/panels.c` | gruppi icona, hover, tooltip | fatto |
| `apps/isocad/src/console.c` | gruppo testo, nessuna string list nel modulo | fatto |

Nota: `isogl_vbo_array_set_transform()` esiste, ma `isocad` non lo usa; la
matrice relativa dei VBO documento è sempre identità. L'overlay a gruppi applica
la trasformazione del gruppo cuocendo le posizioni nello staging, non con
questa API. Con la geometria già in mm di carta, il batch statico non ha
bisogno di una matrice per oggetto.

`CadDocument.next_id` assegna ID monotoni alle entità CAD: non viene riusato e
non dipende più da `count + 1`. I simboli isogl hanno un namespace separato
(`isogl_symbol_id`, monotono con tombstones). Resta da implementare la
rimozione/undo delle entità e il modello gerarchico del CAD.

## Ordine di lavoro consigliato

1. `symgen` puro + `tessellate_arc()` + test **senza contesto GL** (cerchio,
   arco, punta, tolleranza).
2. `entity_bounds()` per tipo e indice AABB/grid; test della broad phase.
3. `entity_hit()` per tipo, window e crossing; test sui casi limite.
4. Pick click/hover e selezione d’area: FBO 16×16 `GL_R32UI` (box 8×8), `pick_id` per
   draw item, pre-filtro distanza dal layout, soglia/raggio configurabili;
   drag sinistro con start in mm, highlight GPU e query CPU
   `cad_select_rect`. Test: interno del cerchio, angolo vuoto di una diagonale,
   tratteggio, due entità sovrapposte.
5. Prima entità reale: la quota. È implementata come simbolo isogl, con
   generatore puro condiviso, registry, explode e trasferimento al CAD. L'app
   usa riferimenti simbolo e selezione taggata. Restano il modello CAD
   gerarchico e il poligono regolare CAD-side.
6. Pagine VBO solo quando il profilo mostra che le draw call per entità sono
   il collo di bottiglia. Fino ad allora resta un VBO per entità: il generatore
   non dipende dal backend. Il batching non è per stile (lo stile è per
   vertice) e non fonde le entità: tiene solo i loro range in un pool.

## Da non fare

- Tenere una copia RAM dei segmenti.
- Fare hit-test sui segmenti invece che sulle entità.
- Usare un pass di pick GPU per la selezione a rettangolo.
- Usare l'overlay o i suoi VBO per la geometria del documento: è solo stato UI.
- Modificare `isogl_model_matrix`/`isogl_view_matrix` dentro
  `isogl_overlay_draw()`: l'overlay usa una proiezione propria.
- Aggiungere un helper che converta le linee dell'overlay in quad spessi: le
  linee UI restano `GL_LINES` a 1 px.
- Usare il solo AABB come filtro del click: il cerchio include l'interno, la
  diagonale include gli angoli vuoti.
- Leggere il framebuffer normale per decidere se fare il pick.
- Usare il colore di visualizzazione o il nome del VBO come ID di pick.
- Rigenerare tutto il documento a ogni modifica.
- Lasciare 96 segmenti fissi per ogni cerchio.
- Introdurre FreeType/Pango o nuove dipendenze senza necessità.
- Assumere thread-safety: lo stato è singleton e legato al contesto corrente.
- Far leggere al CAD i VBO/segmenti interni di un simbolo isogl: solo
  hit/bounds/pick/explode.
- Mettere simboli nel CAD o semantica CAD in isogl.
- Riusare l'ID di un simbolo distrutto o distruggerlo se l'emitter
  dell'explode fallisce.
- Copiare `data_structure/` alla lettera: ID globali fissi, primitive senza ID,
  stringhe non liberate e tabelle duplicate sono solo un riferimento.
- Includere SDL o tipi `SDL_Window*` nel core (`include/isogl/`, `src/`): SDL
  vive solo in `hosts/isogl-sdl`, nella demo e nei test.
- Fare lo swap dentro la libreria: lo swap è dell'ospite.
- Usare le coordinate della finestra invece di quelle locali al viewport.
- Calcolare NDC della selezione una volta sola e cacharlo: va ricalcolato al
  draw perché il viewport può cambiare.
- Far dipendere il parsing del font dal locale del processo: tomlc99 usa
  `strtod`, quindi in locale con virgola decimale gli `advance` diventano 0 e
  i glifi si sovrappongono. `isogl_font_load_from_toml()` forza
  `LC_NUMERIC=C` e ripristina; `test-viewport` copre il caso.

## Build e test

```sh
make                        # core + helper SDL + demo
make lib                    # solo il core, senza SDL installato
make sdl-host               # solo libisogl_sdl.a
make test-symbol-geometry   # headless: geometria pura, niente GL/SDL
make test-panel-layout      # headless: layout pannelli, niente GL/SDL
make test-panel-deck        # headless: slot e parcheggio pagine, niente GL/SDL
make test-overlay           # GL: overlay a gruppi, batch, clip, testo
make test-drawing-load      # GL: loader TOML gerarchico + simboli
make test-symbol-params     # GL: registry tipi C, parametri, get/update
make test-symbols           # GL: fake CAD gerarchico + fixture TOML
make test-viewport          # GL: viewport, coordinate, pick HiDPI, selezione
make test                   # geometry + layout + deck + overlay + drawing + symbols + viewport + core
make check-sdl-free         # nessun riferimento SDL nel core
make example-qt6            # host Qt6 (opzionale)
make example-gtk4           # host GTK4 (opzionale, richiede libgtk-4-dev)
make -B SANITIZE=1 test
```

`tests/test_symbol_geometry.c` e `tests/test_panel_layout.c` girano senza
finestra e senza linkare SDL/GL. `tests/test_overlay.c` usa un contesto GL
(Xvfb in CI) e un FBO. `tests/test_symbols.c` usa un contesto GL e il disegno
strutturato `tests/fixtures/test_drawing.toml`; il fake CAD
`tests/fake_cad.*` non è il CAD di produzione. I test di `tests/test_core.c`
richiedono ancora un contesto GL.

## Riferimenti

- `docs/generatore-di-simboli.html` — documento completo con i diagrammi.
- `README.md` — architettura, contratto, build, install.
- `apps/isocad/README.md` — modello del documento e stato dell'app.
- `examples/demo/architecture.toml` — disegno gerarchico della demo
  (`sheet` → `groups` → `primitives` → `symbols` v2), caricato da
  `examples/demo/drawing_load.c` con trasformazioni di gruppo e promozione
  automatica A3 → A2. `tests/fixtures/test_drawing_v2.toml` è la fixture
  schema v2; `tests/fixtures/test_drawing.toml` resta quella p1x legacy.
- `include/isogl/isogl.h`, `include/isogl/isogl_font.h`,
  `include/isogl/isogl_style.h`, `include/isogl/isogl_overlay.h`,
  `include/isogl/isogl_panel.h` — API, overlay e tabelle normative.
