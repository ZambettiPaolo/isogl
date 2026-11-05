#ifndef ISOCAD_CONSOLE_H
#define ISOCAD_CONSOLE_H

#include <isogl/isogl.h>
#include <stddef.h>

/* Console prototype: a 100-logical-px strip at the bottom of the window.
 * The strip is an opaque overlay drawn with scissor+clear; the text uses a
 * full-window pixel orthographic view passed to isogl_draw_string_list.
 * No second isogl viewport is installed. */

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
    isogl_string_list strings;   /* four panel lines */
    int strings_dirty;

    int window_w, window_h;      /* framebuffer pixels */
    int panel_h;                 /* framebuffer pixels */
    float cap_px, pitch_px, pad_x_px;
    float baseline_px[1 + ISOCAD_CONSOLE_HISTORY_LINES];
    float view[16];              /* full-window pixel ortho, column-major */
} IsocadConsole;

void isocad_console_init(IsocadConsole *c, isogl_font_data *font);
void isocad_console_free(IsocadConsole *c);

/* Idempotent.  panel_h is in framebuffer pixels; the metrics are derived from
 * panel_h/100 so HiDPI needs no extra scale.  Marks the string list dirty
 * when the window or panel size changes. */
void isocad_console_layout(IsocadConsole *c, int fb_w, int fb_h,
                           int panel_h);

/* Pure state operations: no GL calls, safe with font == NULL. */
void isocad_console_text_input(IsocadConsole *c, const char *utf8);
void isocad_console_backspace(IsocadConsole *c);
int  isocad_console_enter(IsocadConsole *c);   /* 1 if a line was echoed */
void isocad_console_clear_input(IsocadConsole *c);

/* Rebuilds if dirty, sets the panel text colour and draws with the active
 * (full-window) viewport. */
void isocad_console_draw(IsocadConsole *c, GLuint font_program);

#endif /* ISOCAD_CONSOLE_H */
