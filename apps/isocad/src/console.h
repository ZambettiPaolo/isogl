#ifndef ISOCAD_CONSOLE_H
#define ISOCAD_CONSOLE_H

#include <isogl/isogl.h>
#include <stddef.h>

/* Console: a 100-logical-px strip at the bottom of the window.
 * The strip is an opaque overlay drawn with scissor+clear; the text uses a
 * full-window pixel orthographic view passed to isogl_draw_string_list.
 * No second isogl viewport is installed.
 *
 * Input handling and parsing are pure state operations (no GL, no SDL):
 * isocad_console_enter() returns the action the application has to perform.
 * The console only owns its text history and the echo/output lines. */

#define ISOCAD_CONSOLE_MAX_INPUT          60
#define ISOCAD_CONSOLE_HISTORY_LINES      3
#define ISOCAD_CONSOLE_CMD_HISTORY        32
#define ISOCAD_CONSOLE_LOGICAL_HEIGHT     100.0f
#define ISOCAD_CONSOLE_LOGICAL_CAP        13.0f
#define ISOCAD_CONSOLE_LOGICAL_PITCH      21.0f
#define ISOCAD_CONSOLE_LOGICAL_BASE0      18.0f
#define ISOCAD_CONSOLE_LOGICAL_PAD_X      8.0f

/* Longest rendered line: a "> " echo plus the maximum input. */
#define ISOCAD_CONSOLE_LINE_MAX (ISOCAD_CONSOLE_MAX_INPUT + 4)

typedef enum {
    ISOCAD_CONSOLE_CMD_NONE = 0,
    ISOCAD_CONSOLE_CMD_FIT,          /* fit the sheet above the panel */
    ISOCAD_CONSOLE_CMD_TOGGLE_STYLE, /* colour-thickness / print colours */
    ISOCAD_CONSOLE_CMD_QUIT          /* leave the application */
} IsocadConsoleCommand;

typedef struct {
    char input[ISOCAD_CONSOLE_MAX_INPUT + 1];
    size_t input_len;

    /* Rendered lines, newest at [0], displayed just above the input line.
       Input echoes already carry the "> " prefix; command output does not. */
    char history[ISOCAD_CONSOLE_HISTORY_LINES][ISOCAD_CONSOLE_LINE_MAX];
    size_t history_count;

    /* Submitted command lines, newest at [0].  Duplicates of the newest
       entry are not stored twice. */
    char cmd_history[ISOCAD_CONSOLE_CMD_HISTORY][ISOCAD_CONSOLE_MAX_INPUT + 1];
    size_t cmd_history_count;

    /* 0 = editing; k = showing cmd_history[k - 1].  The line being edited
       before the first Up is saved in recall_draft. */
    size_t recall_pos;
    char recall_draft[ISOCAD_CONSOLE_MAX_INPUT + 1];

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
void isocad_console_clear_input(IsocadConsole *c);

/* Submit the input line.  The command is echoed, parsed and stored; the
 * returned action is executed by the application, which can append feedback
 * with isocad_console_print().  Empty/blank lines return NONE. */
IsocadConsoleCommand isocad_console_enter(IsocadConsole *c);

/* Command recall: prev is Up, next is Down.  Editing the recalled line
 * leaves recall mode; Down past the newest command restores the draft. */
void isocad_console_history_prev(IsocadConsole *c);
void isocad_console_history_next(IsocadConsole *c);

/* Append one line of output (no "> " prefix).  NULL is ignored. */
void isocad_console_print(IsocadConsole *c, const char *text);

/* Rebuilds if dirty, sets the panel text colour and draws with the active
 * (full-window) viewport. */
void isocad_console_draw(IsocadConsole *c, GLuint font_program);

#endif /* ISOCAD_CONSOLE_H */
