#include "console.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The font vertices are int16: reject a line whose advance would clamp. */
#define CONSOLE_MAX_FONT_ADVANCE 32000.0

static const float CONSOLE_TEXT_COLOR[4] = {0.80f, 0.86f, 0.92f, 1.0f};
static const float CONSOLE_PANEL_BG[4] = {0.030f, 0.039f, 0.047f, 1.0f};
static const float CONSOLE_PANEL_EDGE[4] = {0.20f, 0.24f, 0.30f, 1.0f};

static void console_seed_history(IsocadConsole *c) {
    c->history_count = ISOCAD_CONSOLE_HISTORY_LINES;
    /* [0] is the newest, displayed just above the input line. */
    snprintf(c->history[0], ISOCAD_CONSOLE_LINE_MAX,
             "type help for commands");
    snprintf(c->history[1], ISOCAD_CONSOLE_LINE_MAX,
             "up/down: history");
    snprintf(c->history[2], ISOCAD_CONSOLE_LINE_MAX,
             "isocad console");
}

void isocad_console_init(IsocadConsole *c, isogl_font_data *font) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->font = font;
    c->strings_dirty = 1;
    console_seed_history(c);
}

void isocad_console_free(IsocadConsole *c) {
    if (!c) return;
    isogl_string_list_free(&c->strings);
    memset(&c->strings, 0, sizeof(c->strings));
}

static int console_line_fits(const IsocadConsole *c, const char *input) {
    if (!c || !c->font) return 1;

    char line[ISOCAD_CONSOLE_MAX_INPUT + 8];
    int n = snprintf(line, sizeof(line), "> %s_", input);
    if (n < 0) return 0;

    return isogl_font_text_advance(c->font, line) <= CONSOLE_MAX_FONT_ADVANCE;
}

void isocad_console_layout(IsocadConsole *c, int fb_w, int fb_h,
                           int panel_h) {
    if (!c || fb_w <= 0 || fb_h <= 0 || panel_h <= 0 || panel_h > fb_h) {
        return;
    }

    if (c->window_w == fb_w && c->window_h == fb_h &&
        c->panel_h == panel_h && c->cap_px > 0.0f) {
        return;
    }

    c->window_w = fb_w;
    c->window_h = fb_h;
    c->panel_h = panel_h;

    /* If the panel is clamped on a tiny window, u < 1 and the text shrinks
       with it; on HiDPI, u = dpr and the metrics are in framebuffer pixels. */
    float u = (float)panel_h / ISOCAD_CONSOLE_LOGICAL_HEIGHT;
    c->cap_px = ISOCAD_CONSOLE_LOGICAL_CAP * u;
    c->pitch_px = ISOCAD_CONSOLE_LOGICAL_PITCH * u;
    c->pad_x_px = ISOCAD_CONSOLE_LOGICAL_PAD_X * u;

    for (int i = 0; i < 1 + ISOCAD_CONSOLE_HISTORY_LINES; ++i) {
        c->baseline_px[i] =
            (ISOCAD_CONSOLE_LOGICAL_BASE0 +
             ISOCAD_CONSOLE_LOGICAL_PITCH * (float)i) * u;
    }

    memset(c->overlay_view, 0, sizeof(c->overlay_view));
    c->overlay_view[0] = 2.0f / (float)fb_w;
    c->overlay_view[5] = 2.0f / (float)fb_h;
    c->overlay_view[10] = 1.0f;
    c->overlay_view[12] = -1.0f;
    c->overlay_view[13] = -1.0f;
    c->overlay_view[15] = 1.0f;

    c->strings_dirty = 1;
}

/* -------------------------------------------------------------------------
 * History and recall
 * ---------------------------------------------------------------------- */

static void console_history_push(IsocadConsole *c, const char *line) {
    if (!c || !line) return;

    size_t shift = c->history_count;
    if (shift >= ISOCAD_CONSOLE_HISTORY_LINES) {
        shift = ISOCAD_CONSOLE_HISTORY_LINES - 1;
    }
    for (size_t i = shift; i > 0; --i) {
        memcpy(c->history[i], c->history[i - 1],
               sizeof(c->history[i]));
    }
    snprintf(c->history[0], sizeof(c->history[0]), "%s", line);

    if (c->history_count < ISOCAD_CONSOLE_HISTORY_LINES) {
        c->history_count++;
    }
    c->strings_dirty = 1;
}

static void console_cmd_history_push(IsocadConsole *c, const char *line) {
    if (!c || !line) return;

    /* Consecutive duplicates would only waste a recall step. */
    if (c->cmd_history_count > 0 &&
        strcmp(c->cmd_history[0], line) == 0) {
        return;
    }

    size_t shift = c->cmd_history_count;
    if (shift >= ISOCAD_CONSOLE_CMD_HISTORY) {
        shift = ISOCAD_CONSOLE_CMD_HISTORY - 1;
    }
    for (size_t i = shift; i > 0; --i) {
        memcpy(c->cmd_history[i], c->cmd_history[i - 1],
               sizeof(c->cmd_history[i]));
    }
    snprintf(c->cmd_history[0], sizeof(c->cmd_history[0]), "%s", line);

    if (c->cmd_history_count < ISOCAD_CONSOLE_CMD_HISTORY) {
        c->cmd_history_count++;
    }
}

static void console_set_input(IsocadConsole *c, const char *text) {
    size_t n = text ? strlen(text) : 0;
    if (n > ISOCAD_CONSOLE_MAX_INPUT) n = ISOCAD_CONSOLE_MAX_INPUT;

    if (n > 0) memcpy(c->input, text, n);
    c->input[n] = '\0';
    c->input_len = n;
    c->strings_dirty = 1;
}

void isocad_console_history_prev(IsocadConsole *c) {
    if (!c || c->cmd_history_count == 0) return;

    if (c->recall_pos == 0) {
        memcpy(c->recall_draft, c->input, c->input_len + 1);
    }
    if (c->recall_pos < c->cmd_history_count) {
        c->recall_pos++;
        console_set_input(c, c->cmd_history[c->recall_pos - 1]);
    }
}

void isocad_console_history_next(IsocadConsole *c) {
    if (!c || c->recall_pos == 0) return;

    c->recall_pos--;
    if (c->recall_pos == 0) {
        console_set_input(c, c->recall_draft);
    } else {
        console_set_input(c, c->cmd_history[c->recall_pos - 1]);
    }
}

/* -------------------------------------------------------------------------
 * Editing
 * ---------------------------------------------------------------------- */

void isocad_console_text_input(IsocadConsole *c, const char *utf8) {
    if (!c || !utf8) return;

    for (const unsigned char *p = (const unsigned char *)utf8; *p; ++p) {
        if (*p < 0x20 || *p > 0x7E) continue;
        if (c->input_len >= ISOCAD_CONSOLE_MAX_INPUT) break;

        char candidate[ISOCAD_CONSOLE_MAX_INPUT + 1];
        memcpy(candidate, c->input, c->input_len);
        candidate[c->input_len] = (char)*p;
        candidate[c->input_len + 1] = '\0';
        if (!console_line_fits(c, candidate)) break;

        c->recall_pos = 0; /* editing leaves recall mode */
        c->input[c->input_len++] = (char)*p;
        c->input[c->input_len] = '\0';
        c->strings_dirty = 1;
    }
}

void isocad_console_backspace(IsocadConsole *c) {
    if (!c || c->input_len == 0) return;

    c->recall_pos = 0;
    c->input[--c->input_len] = '\0';
    c->strings_dirty = 1;
}

void isocad_console_clear_input(IsocadConsole *c) {
    if (!c) return;

    c->recall_pos = 0;
    if (c->input_len == 0) return;
    c->input[0] = '\0';
    c->input_len = 0;
    c->strings_dirty = 1;
}

void isocad_console_print(IsocadConsole *c, const char *text) {
    if (!c || !text) return;
    console_history_push(c, text);
}

/* -------------------------------------------------------------------------
 * Command parsing
 * ---------------------------------------------------------------------- */

static char *console_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;

    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    return s;
}

static int console_command_is(const char *word, const char *literal) {
    for (; *literal; ++word, ++literal) {
        char ch = *word;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (ch != *literal) return 0;
    }
    return *word == '\0';
}

IsocadConsoleCommand isocad_console_enter(IsocadConsole *c) {
    if (!c || c->input_len == 0) return ISOCAD_CONSOLE_CMD_NONE;

    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    memcpy(line, c->input, c->input_len + 1);
    char *cmdline = console_trim(line);

    /* A line of spaces is not a command. */
    if (*cmdline == '\0') {
        isocad_console_clear_input(c);
        return ISOCAD_CONSOLE_CMD_NONE;
    }

    char echo[ISOCAD_CONSOLE_LINE_MAX];
    snprintf(echo, sizeof(echo), "> %s", cmdline);
    console_history_push(c, echo);
    console_cmd_history_push(c, cmdline);

    c->input[0] = '\0';
    c->input_len = 0;
    c->recall_pos = 0;
    c->recall_draft[0] = '\0';
    c->strings_dirty = 1;

    /* Split the command word from its arguments. */
    char *arg = cmdline;
    while (*arg && *arg != ' ' && *arg != '\t') arg++;
    if (*arg) {
        *arg++ = '\0';
        arg = console_trim(arg);
    }

    if (console_command_is(cmdline, "fit")) {
        return ISOCAD_CONSOLE_CMD_FIT;
    }
    if (console_command_is(cmdline, "color") ||
        console_command_is(cmdline, "colour") ||
        console_command_is(cmdline, "colors") ||
        console_command_is(cmdline, "colours") ||
        console_command_is(cmdline, "thickness") ||
        console_command_is(cmdline, "style")) {
        return ISOCAD_CONSOLE_CMD_TOGGLE_STYLE;
    }
    if (console_command_is(cmdline, "quit") ||
        console_command_is(cmdline, "exit")) {
        return ISOCAD_CONSOLE_CMD_QUIT;
    }
    if (console_command_is(cmdline, "help") ||
        console_command_is(cmdline, "?")) {
        isocad_console_print(c, "commands: fit color clear echo help quit");
        return ISOCAD_CONSOLE_CMD_NONE;
    }
    if (console_command_is(cmdline, "clear") ||
        console_command_is(cmdline, "cls")) {
        c->history_count = 0;
        c->strings_dirty = 1;
        return ISOCAD_CONSOLE_CMD_NONE;
    }
    if (console_command_is(cmdline, "echo")) {
        isocad_console_print(c, *arg ? arg : "echo: missing text");
        return ISOCAD_CONSOLE_CMD_NONE;
    }

    char message[ISOCAD_CONSOLE_LINE_MAX];
    snprintf(message, sizeof(message),
             "unknown command: %.24s; type help", cmdline);
    isocad_console_print(c, message);
    return ISOCAD_CONSOLE_CMD_NONE;
}

/* -------------------------------------------------------------------------
 * Rendering
 * ---------------------------------------------------------------------- */

static void console_append(IsocadConsole *c, const char *text,
                           float x, float y) {
    isogl_glyph_string *s = isogl_create_glyph_string(text, c->font);
    if (!s) return;

    if (!isogl_string_list_append(&c->strings, s, x, y, c->cap_px, 1.0f,
                                  0.0f, c->font, 0)) {
        isogl_free_glyph_string(s);
    }
}

static void console_rebuild(IsocadConsole *c) {
    isogl_string_list_free(&c->strings);
    memset(&c->strings, 0, sizeof(c->strings));

    if (!c->font || !(c->cap_px > 0.0f)) {
        c->strings_dirty = 0;
        return;
    }

    char line[ISOCAD_CONSOLE_MAX_INPUT + 8];

    snprintf(line, sizeof(line), "> %s_", c->input);
    console_append(c, line, c->pad_x_px, c->baseline_px[0]);

    size_t n = c->history_count;
    if (n > ISOCAD_CONSOLE_HISTORY_LINES) n = ISOCAD_CONSOLE_HISTORY_LINES;
    for (size_t i = 0; i < n; ++i) {
        console_append(c, c->history[i], c->pad_x_px,
                       c->baseline_px[i + 1]);
    }

    c->strings_dirty = 0;
}

void isocad_console_draw(IsocadConsole *c, GLuint font_program) {
    if (!c || c->panel_h <= 0 ||
        c->window_w <= 0 || c->window_h <= 0) {
        return;
    }

    /* The overlay is drawn after the scene: the panel quad first, then its
       text, both through isogl with the same orthographic matrix. */
    isogl_draw_rect(c->overlay_view, 0.0f, 0.0f,
                    (float)c->window_w, (float)c->panel_h,
                    CONSOLE_PANEL_BG);
    if (c->panel_h > 1) {
        isogl_draw_rect(c->overlay_view, 0.0f,
                        (float)c->panel_h - 1.0f,
                        (float)c->window_w, 1.0f, CONSOLE_PANEL_EDGE);
    }

    if (!c->font || font_program == 0) return;
    if (c->strings_dirty) console_rebuild(c);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    isogl_font_set_color(CONSOLE_TEXT_COLOR);
    isogl_draw_string_list(&c->strings, font_program,
                           identity, c->overlay_view, identity);
}
