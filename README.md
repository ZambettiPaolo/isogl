# isogl

Renderer OpenGL per disegno tecnico 2D conforme alle norme ISO 128 (linee) e
ISO 3098 (testo), pensato per un CAD minimale orientato alla stampa.

Non è una libreria neutra: espone e fa rispettare le norme. Tipi di linea,
pesi, altezze testo, formati foglio e la mappa colore→spessore vivono in un solo
modulo (`isogl_style`), usato sia dal C sia dagli shader.

## Architettura

| Modulo | Responsabilità |
|---|---|
| `isogl_style` | tabelle normative: 15 tipi di linea, 8 pesi, 8 altezze testo, formati A4-A0, colori-spessore |
| `isogl` | init/cleanup, shader, viewport, foglio, matrici view/model, zoom/pan, oggetti segmento |
| `isogl_font` | font vettoriale TOML, stringhe, VBO per stringa |
| `isogl_symbol*` | geometria pura e registry GL dei simboli (quota, center mark) |
| `isogl_overlay` | overlay screen-space a gruppi: quad, linee e testo batchati, dirty per gruppo, clip e layer |
| `isogl_panel*` | layout puro dei pannelli (bordi, separatori, content rect) ed emissione dei quad |
| `shaders/vs|gs|fs_segment` | unpack attributi, filtro maschere, clip, tratteggio |
| `shaders/vs|fs_sheet` | foglio |
| `shaders/vs|fs_overlay` | quad e linee dell'overlay, colore e clip per vertice |
| `shaders/vs|fs_font` | testo con un VBO di `GL_LINES` per stringa |

`libisogl.a` non dipende da SDL: usa solo OpenGL, glad e tomlc99. SDL compare
solo nel pacchetto opzionale `isogl-sdl` (helper di hosting), nella demo e nei
test che hanno bisogno di un contesto GL.

## Viewport e coordinate

L'ospite possiede finestra e contesto GL; isogl usa il contesto corrente e
riceve il rettangolo di viewport in pixel di framebuffer:

```c
isogl_viewport vp = {0, 0, fb_width, fb_height};  /* origine GL, basso-sinistra */
isogl_set_viewport(&vp);
isogl_begin_frame();      /* riasserisce lo stato GL del toolkit */
isogl_clear(...);         /* limitato al viewport, con scissor salva/ripristina */
... draw ...
/* lo swap è dell'ospite: SDL_GL_SwapWindow, GtkGLArea, QOpenGLWidget */
```

Tutte le altre coordinate in pixel (`isogl_pick_at`, `screen_to_world`,
`world_to_screen`, zoom, pan, rettangolo di selezione) sono **locali al
viewport, origine in alto a sinistra**, come gli eventi dei toolkit. Per un
viewport a schermo intero coincidono con le coordinate della finestra; per un
pannello si sottrae l'angolo superiore sinistro del widget.

`isogl_pick_at()` riceve anche `extent_px`, il lato del box di pick in pixel di
framebuffer. Su HiDPI l'ospite passa `lround(ISOGL_PICK_DEFAULT_EXTENT * dpr)`
per mantenere il box costante in punti logici.

Host verificati: SDL2 (`hosts/isogl-sdl`), GTK4 `GtkGLArea`
(`examples/gtk4`), Qt6 `QOpenGLWidget` (`examples/qt6`). GTK4 e Qt6 usano i
loro contesti EGL nativi, quindi funzionano su Wayland senza
`SDL_CreateWindowFrom` né handle di finestra estranei.

Spazi di coordinate:

```
clip = uView (aspect) * uModel (zoom/pan) * uRelative (oggetto) * posizione
```

Le primitive sono in mm di carta; `uScale` (scala foglio) converte le distanze
in mm per il tratteggio. Il foglio è il riferimento: `isogl_set_sheet` riceve
larghezza/altezza in mm e la scala normalizzata (es. `{1,2}` per 1:2).

Modello in GPU: ogni parte è un VBO (`isogl_vbo_object`) con la sua matrice.
`isogl_vbo_array_update(id, lista)` ri-carica **solo** il VBO della parte
modificata; il resto del modello resta in memoria video. Non ci sono upload nel
loop di rendering.

Testo: font vettoriali proprietari caricati da TOML (nessuna dipendenza da
FreeType/Pango). Ogni stringa è un VBO con `first`/`count` per
`glMultiDrawArrays`. Le coordinate dei glifi sono `int16`; le stringhe più larghe
di ~32767 unità vengono clampate con un avviso (limite documentato).

Il parsing TOML è **indipendente dal locale**: `isogl_font_load_from_toml()`
forza `LC_NUMERIC=C` e ripristina il locale dell'ospite. Senza, in un locale
con virgola decimale (`it_IT`, `de_DE`, `fr_FR`) i valori `advance = 320.0`
non verrebbero letti, gli advance resterebbero zero e i glifi si
sovrapporrebbero. `make test-viewport` copre il caso.

Larghezza linee: **1 px per scelta deliberata**. Lo spessore ISO è un dato del
modello e viene visualizzato tramite la mappa colore→spessore (`T` alterna
colori-spessore e colori-stampa, come in AutoCAD).

## Contratto

- l'applicazione possiede finestra e contesto; la libreria non li crea;
- `gladLoadGLLoader()` va chiamato prima di `isogl_init*`;
- l'ospite imposta il viewport con `isogl_set_viewport()` e chiama
  `isogl_begin_frame()` dopo il lavoro GL del toolkit;
- lo swap dei buffer è dell'ospite, non della libreria;
- un solo contesto corrente, nessuna thread-safety, stato singleton;
- `isogl_cleanup()` va chiamato con il contesto corrente ed è idempotente;
- gli shader sono caricati da `ISOGL_ASSET_DIR`, dal path esplicito di
  `isogl_init_with_asset_dir()`, dal default di build (`$(DATADIR)`) o da `"."`;
- solo `ISOGL_VBO_SEGMENT` è implementato.

## Simboli isogl

`libisogl` possiede i simboli (quota, center mark): geometria pura in
`isogl_symbol_geometry.*`, registry/render/pick in `isogl_symbol.*`. Il CAD
conserva solo l’ID opaco del simbolo, usa `isogl_symbol_hit/bounds/pick` e, per
manipolare, `isogl_symbol_explode`, che emette segmenti e run di testo e
distrugge il simbolo. La geometria CAD resta del CAD: isogl riceve solo VBO e
stringhe.

## Build e install

```sh
make                     # build/libisogl.a + libisogl_sdl.a + build/isogl-demo
make lib                 # solo il core, compila senza SDL installato
make test-symbol-geometry # headless: geometria pura, niente GL/SDL
make test-panel-layout    # headless: matematica dei pannelli, niente GL/SDL
make test-overlay         # GL: overlay a gruppi, batch, clip, testo
make test-symbols         # GL: fake CAD gerarchico + fixture TOML
make test-viewport        # GL: viewport, coordinate, pick HiDPI, selezione
make test                 # geometria + layout + overlay + symbols + viewport + test FBO
make check-sdl-free       # il core non deve contenere riferimenti SDL
make example-qt6          # host Qt6 (opzionale, richiede Qt6)
make example-gtk4         # host GTK4 (opzionale, richiede libgtk-4-dev)
make install PREFIX="$HOME/.local"
```

The core library is compiled without SDL headers: a stray `#include <SDL.h>`
in `src/` or `include/isogl/` fails the build. `make check-sdl-free` also
checks `nm -u build/libisogl.a` for SDL symbols.

Install layout:

```
$PREFIX/include/isogl/{isogl.h,isogl_font.h,isogl_style.h,isogl_symbol.h,isogl_symbol_geometry.h,isogl_overlay.h,isogl_panel.h,glad.h,khrplatform.h}
$PREFIX/include/isogl-sdl/isogl_sdl.h
$PREFIX/lib/libisogl.a
$PREFIX/lib/libisogl_sdl.a
$PREFIX/lib/pkgconfig/isogl.pc
$PREFIX/lib/pkgconfig/isogl-sdl.pc
$PREFIX/share/isogl/shaders/*.glsl
$PREFIX/share/isogl/fonts/ReliefSingleLineLinear.toml
```

`isogl.pc` non richiede SDL; `isogl-sdl.pc` richiede `isogl sdl2`.

Sanitizer:

```sh
make -B SANITIZE=1 test
```

## Uso minimo

```c
/* L'ospite ha reso corrente un contesto GL 3.3 core e ha caricato glad. */
if (isogl_init_with_asset_dir(NULL) != 0) { /* errore */ }
isogl_set_sheet(420.0f, 297.0f, (isogl_scale){1, 1}); // A3, 1:1
isogl_zoom_all_sheet();

isogl_segment_list list;
isogl_segment_list_init(&list, 64);
isogl_segment_list_push_pair(&list, v0, v1);
GLuint part = isogl_vbo_array_add(&parts, &list, ISOGL_VBO_SEGMENT);
isogl_segment_list_free(&list);

// modifica di una parte
isogl_vbo_array_update(&parts, part, &new_list);

// ogni frame, dopo il lavoro GL del toolkit
isogl_viewport vp = {0, 0, fb_width, fb_height};
isogl_set_viewport(&vp);
isogl_begin_frame();
isogl_clear(0.08f, 0.09f, 0.10f, 1.0f);

isogl_draw_sheet();
isogl_draw_all_vbo_objects(&parts);
isogl_draw_string_list(&strings, isogl_shader_program_font,
                       isogl_model_matrix, isogl_view_matrix, identity);

isogl_string_list_free(&strings);
isogl_vbo_array_free(&parts);
isogl_cleanup();
```

## Monorepo

- `include/`, `src/`, `shaders/`, `fonts/` — la libreria (core senza SDL);
- `hosts/isogl-sdl/` — helper opzionale SDL2 (`libisogl_sdl.a`, `isogl-sdl.pc`);
- `examples/demo/` — demo/smoke test SDL; disegna l'architettura di isogl su A3
  con le sole primitive della libreria (`architecture.c`);
- `examples/gtk4/` — host GTK4 (`GtkGLArea`);
- `examples/qt6/` — host Qt6 (`QOpenGLWidget`);
- `apps/isocad/` — app CAD minima che consuma la libreria installata via
  `pkg-config` (non compila i sorgenti di isogl);
- `tests/` — test FBO (stile, segmenti, update incrementale, font, viewport,
  pick, simboli, cleanup);
- `tools/` — converter SVG → TOML.

```sh
make PREFIX="$HOME/.local" all install
PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig" make -C apps/isocad
./apps/isocad/build/isocad
make example-qt6      # richiede Qt6
make example-gtk4     # richiede libgtk-4-dev
```

La demo `build/isogl-demo` contiene un disegno A3 dell'architettura di isogl:
host/app, core `libisogl` SDL-free, overlay/pannelli, font/style/simboli e
pipeline GPU. Ogni riquadro, freccia e etichetta è un segmento o una stringa
isogl; nessun GL diretto nel builder.

```sh
./build/isogl-demo --assets "$PWD/_install/share/isogl"
```

Comandi demo: rotella = zoom, drag sinistro = pan, `Z` = fit, `T` = alterna
colori-spessore/stampa, `Q` o `Esc` = esci.

La CI (`.github/workflows/ci.yml`) compila, esegue i test sotto Xvfb, verifica
che il core resti SDL-free, installa la libreria, costruisce isocad e i due
esempi host.

Su Wayland: l'helper SDL e gli esempi GTK4/Qt6 usano il contesto EGL del
toolkit. Non si usa `SDL_CreateWindowFrom`, che non ha un equivalente Wayland
affidabile.

## Licenza

MIT — vedi `LICENSE`. Le dipendenze vendored sono elencate in
`third_party/README.md`.
