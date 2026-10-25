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
| `isogl` | init/cleanup, shader, foglio, matrici view/model, zoom/pan, oggetti segmento |
| `isogl_font` | font vettoriale TOML, stringhe, VBO per stringa |
| `shaders/vs|gs|fs_segment` | unpack attributi, filtro maschere, clip, tratteggio |
| `shaders/vs|fs_sheet` | foglio |
| `shaders/vs|fs_font` | testo con `glMultiDrawArrays(GL_LINE_STRIP)` |

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

Larghezza linee: **1 px per scelta deliberata**. Lo spessore ISO è un dato del
modello e viene visualizzato tramite la mappa colore→spessore (`T` alterna
colori-spessore e colori-stampa, come in AutoCAD).

## Contratto

- l'applicazione possiede finestra e contesto; la libreria non li crea;
- `gladLoadGLLoader()` va chiamato prima di `isogl_init*`;
- un solo contesto corrente, nessuna thread-safety, stato singleton;
- `isogl_cleanup()` va chiamato con il contesto corrente ed è idempotente;
- gli shader sono caricati da `ISOGL_ASSET_DIR`, dal path esplicito di
  `isogl_init_with_asset_dir()`, dal default di build (`$(DATADIR)`) o da `"."`;
- solo `ISOGL_VBO_SEGMENT` è implementato.

## Build e install

```sh
make                     # build/libisogl.a + build/isogl-demo
make test                # test FBO: stile, segmenti, update, font, cleanup
make install PREFIX="$HOME/.local"
```

Install layout:

```
$PREFIX/include/isogl/{isogl.h,isogl_font.h,isogl_style.h,glad.h,khrplatform.h}
$PREFIX/lib/libisogl.a
$PREFIX/lib/pkgconfig/isogl.pc
$PREFIX/share/isogl/shaders/*.glsl
$PREFIX/share/isogl/fonts/ReliefSingleLineLinear.toml
```

Sanitizer:

```sh
make -B SANITIZE=1 test
```

## Uso minimo

```c
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

isogl_draw_sheet();
isogl_draw_all_vbo_objects(&parts);
isogl_draw_string_list(&strings, isogl_shader_program_font,
                       isogl_model_matrix, isogl_view_matrix, identity);

isogl_string_list_free(&strings);
isogl_vbo_array_free(&parts);
isogl_cleanup();
```

## Monorepo

- `include/`, `src/`, `shaders/`, `fonts/` — la libreria;
- `examples/demo/` — demo/smoke test;
- `apps/isocad/` — app CAD minima che consuma la libreria installata via
  `pkg-config` (non compila i sorgenti di isogl);
- `tests/` — test FBO (stile, segmenti, update incrementale, font, cleanup);
- `tools/` — converter SVG → TOML.

```sh
make PREFIX="$HOME/.local" all install
make -C apps/isocad
./apps/isocad/build/isocad
```

La CI (`.github/workflows/ci.yml`) compila, esegue i test sotto Xvfb, installa
la libreria e costruisce l'app.

## Licenza

MIT — vedi `LICENSE`. Le dipendenze vendored sono elencate in
`third_party/README.md`.
