#ifndef ISOCAD_CONSOLE_H
#define ISOCAD_CONSOLE_H

#include <isogl/isogl.h>
#include <isogl/isogl_overlay.h>
#include <isogl/isogl_panel.h>
#include <stddef.h>

/* Console prototype: a 100-logical-px strip at the bottom of the window.
 * The strip background is emitted by the panel helper; this module owns the
 * text state and emits its lines into an overlay group. */

#define ISOCAD_CONSOLE_MAX_INPUT          60
#define ISOCAD_CONSOLE_HISTORY_LINES      3
#define ISOCAD_CONSOLE_LOGICAL_HEIGHT     100.0f
#define ISOCAD_CONSOLE_LOGICAL_CAP        13.0f
#define ISOCAD_CONSOLE_LOGICAL_PITCH      21.0f
#define ISOCAD_CONSOLE_LOGICAL_BASE0      18.0f
#define ISOCAD_CONSOLE_LOGICAL_PAD_X      8.0f

typedef struct {
    char input[ISOCAD_CONSOLE_MAX_INPUT + 1];
    size_t input_len;

    /* Newest echoed line at [0]; lines are displayed oldest at the top,
       newest just above the input. */
    char history[ISOCAD_CONSOLE_HISTORY_LINES][ISOCAD_CONSOLE_MAX_INPUT + 1];
    size_t history_count;

    isogl_font_data *font;       /* not owned */
    isogl_overlay *overlay;      /* not owned */
    isogl_overlay_group group;

    int dirty;
    int panel_h;                 /* framebuffer pixels */
    float cap_px, pitch_px, pad_x_px;
    float baseline_px[1 + ISOCAD_CONSOLE_HISTORY_LINES];
} IsocadConsole;

void isocad_console_init(IsocadConsole *c, isogl_font_data *font,
                         isogl_overlay *overlay);
void isocad_console_free(IsocadConsole *c);

/* Idempotent.  Metrics are derived from the bottom strip height so HiDPI
 * needs no extra scale.  Marks the text dirty when the size changes. */
void isocad_console_layout(IsocadConsole *c,
                           const isogl_panel_layout *layout);

/* Pure state operations: no GL calls, safe with font == NULL. */
void isocad_console_text_input(IsocadConsole *c, const char *utf8);
void isocad_console_backspace(IsocadConsole *c);
int  isocad_console_enter(IsocadConsole *c);   /* 1 if a line was echoed */
void isocad_console_clear_input(IsocadConsole *c);

/* Emit the text lines into the overlay group when the content or layout
 * changed.  The overlay draws them later. */
void isocad_console_emit(IsocadConsole *c, const isogl_panel_layout *layout);

#endif /* ISOCAD_CONSOLE_H */
