#include "console.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The font vertices are int16: reject a line whose advance would clamp. */
#define CONSOLE_MAX_FONT_ADVANCE 32000.0

static const float CONSOLE_TEXT_COLOR[4] = {0.80f, 0.86f, 0.92f, 1.0f};

static void console_seed_history(IsocadConsole *c) {
    c->history_count = ISOCAD_CONSOLE_HISTORY_LINES;
    /* [0] is the newest, displayed just above the input line. */
    snprintf(c->history[0], sizeof(c->history[0]),
             "Ctrl+F fit  Ctrl+T thickness  Ctrl+Q quit");
    snprintf(c->history[1], sizeof(c->history[1]),
             "type text and press Enter to echo");
    snprintf(c->history[2], sizeof(c->history[2]),
             "isocad console (prototype)");
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

    memset(c->view, 0, sizeof(c->view));
    c->view[0] = 2.0f / (float)fb_w;
    c->view[5] = 2.0f / (float)fb_h;
    c->view[10] = 1.0f;
    c->view[12] = -1.0f;
    c->view[13] = -1.0f;
    c->view[15] = 1.0f;

    c->strings_dirty = 1;
}

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

        c->input[c->input_len++] = (char)*p;
        c->input[c->input_len] = '\0';
        c->strings_dirty = 1;
    }
}

void isocad_console_backspace(IsocadConsole *c) {
    if (!c || c->input_len == 0) return;
    c->input[--c->input_len] = '\0';
    c->strings_dirty = 1;
}

int isocad_console_enter(IsocadConsole *c) {
    if (!c || c->input_len == 0) return 0;

    for (size_t i = ISOCAD_CONSOLE_HISTORY_LINES - 1; i > 0; --i) {
        memcpy(c->history[i], c->history[i - 1], sizeof(c->history[i]));
    }
    memcpy(c->history[0], c->input, c->input_len + 1);
    if (c->history_count < ISOCAD_CONSOLE_HISTORY_LINES) {
        c->history_count++;
    }

    c->input[0] = '\0';
    c->input_len = 0;
    c->strings_dirty = 1;
    return 1;
}

void isocad_console_clear_input(IsocadConsole *c) {
    if (!c || c->input_len == 0) return;
    c->input[0] = '\0';
    c->input_len = 0;
    c->strings_dirty = 1;
}

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
        snprintf(line, sizeof(line), "> %s", c->history[i]);
        console_append(c, line, c->pad_x_px, c->baseline_px[i + 1]);
    }

    c->strings_dirty = 0;
}

void isocad_console_draw(IsocadConsole *c, GLuint font_program) {
    if (!c || !c->font || c->panel_h <= 0 ||
        c->window_w <= 0 || c->window_h <= 0 || font_program == 0) {
        return;
    }

    if (c->strings_dirty) console_rebuild(c);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    isogl_font_set_color(CONSOLE_TEXT_COLOR);
    isogl_draw_string_list(&c->strings, font_program,
                           identity, c->view, identity);
}
