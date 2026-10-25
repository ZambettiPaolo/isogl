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
```

Comandi:

- rotella: zoom ancorato al cursore
- drag sinistro: pan
- `F`: fit del foglio
- `T`: alterna colori-spessore / colori-stampa
- `Esc` o `Q`: esci

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
ISO, zoom/pan, modalità colori-spessore/stampa. Editing, selezione, UI layer,
archi, polilinee ed export/stampa sono i passi successivi.
