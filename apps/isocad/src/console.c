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

void isocad_console_init(IsocadConsole *c, isogl_font_data *font,
                         isogl_overlay *overlay) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->font = font;
    c->overlay = overlay;
    c->dirty = 1;
    console_seed_history(c);

    if (overlay) {
        c->group = isogl_overlay_group_create(overlay, 0);
        isogl_overlay_group_set_visible(overlay, c->group, 1);
    }
}

void isocad_console_free(IsocadConsole *c) {
    if (!c) return;
    if (c->overlay && c->group != ISOGL_OVERLAY_GROUP_INVALID) {
        isogl_overlay_group_destroy(c->overlay, c->group);
    }
    memset(c, 0, sizeof(*c));
}

static int console_line_fits(const IsocadConsole *c, const char *input) {
    if (!c || !c->font) return 1;

    char line[ISOCAD_CONSOLE_MAX_INPUT + 8];
    int n = snprintf(line, sizeof(line), "> %s_", input);
    if (n < 0) return 0;

    return isogl_font_text_advance(c->font, line) <= CONSOLE_MAX_FONT_ADVANCE;
}

void isocad_console_layout(IsocadConsole *c,
                           const isogl_panel_layout *layout) {
    if (!c || !layout || !c->overlay) return;

    int panel_h = layout->bottom.height;
    if (panel_h <= 0) {
        c->panel_h = 0;
        c->dirty = 1;
        return;
    }

    if (c->panel_h == panel_h && c->cap_px > 0.0f) return;

    c->panel_h = panel_h;
    float u = (float)panel_h / ISOCAD_CONSOLE_LOGICAL_HEIGHT;
    c->cap_px = ISOCAD_CONSOLE_LOGICAL_CAP * u;
    c->pitch_px = ISOCAD_CONSOLE_LOGICAL_PITCH * u;
    c->pad_x_px = ISOCAD_CONSOLE_LOGICAL_PAD_X * u;

    for (int i = 0; i < 1 + ISOCAD_CONSOLE_HISTORY_LINES; ++i) {
        c->baseline_px[i] =
            (ISOCAD_CONSOLE_LOGICAL_BASE0 +
             ISOCAD_CONSOLE_LOGICAL_PITCH * (float)i) * u;
    }

    c->dirty = 1;
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
        c->dirty = 1;
    }
}

void isocad_console_backspace(IsocadConsole *c) {
    if (!c || c->input_len == 0) return;
    c->input[--c->input_len] = '\0';
    c->dirty = 1;
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
    c->dirty = 1;
    return 1;
}

void isocad_console_clear_input(IsocadConsole *c) {
    if (!c || c->input_len == 0) return;
    c->input[0] = '\0';
    c->input_len = 0;
    c->dirty = 1;
}

void isocad_console_emit(IsocadConsole *c,
                         const isogl_panel_layout *layout) {
    if (!c || !layout || !c->overlay || !c->dirty) return;

    isogl_overlay_group_clear(c->overlay, c->group);
    isogl_overlay_group_set_transform(c->overlay, c->group, 0.0f, 0.0f, 1.0f);
    isogl_overlay_group_set_layer(c->overlay, c->group, 0);

    if (c->panel_h <= 0 || layout->bottom.height <= 0) {
        isogl_overlay_group_set_visible(c->overlay, c->group, 0);
        c->dirty = 0;
        return;
    }

    isogl_overlay_group_set_visible(c->overlay, c->group, 1);
    isogl_overlay_group_set_clip(c->overlay, c->group, 1,
                                 (float)layout->bottom.x,
                                 (float)layout->bottom.y,
                                 (float)layout->bottom.width,
                                 (float)layout->bottom.height);

    char line[ISOCAD_CONSOLE_MAX_INPUT + 8];

    snprintf(line, sizeof(line), "> %s_", c->input);
    float y0 = (float)layout->height - c->baseline_px[0];
    isogl_overlay_group_text(c->overlay, c->group, line,
                             c->pad_x_px, y0, c->cap_px, 0.0f,
                             CONSOLE_TEXT_COLOR);

    size_t n = c->history_count;
    if (n > ISOCAD_CONSOLE_HISTORY_LINES) n = ISOCAD_CONSOLE_HISTORY_LINES;
    for (size_t i = 0; i < n; ++i) {
        snprintf(line, sizeof(line), "> %s", c->history[i]);
        float y = (float)layout->height - c->baseline_px[i + 1];
        isogl_overlay_group_text(c->overlay, c->group, line,
                                 c->pad_x_px, y, c->cap_px, 0.0f,
                                 CONSOLE_TEXT_COLOR);
    }

    c->dirty = 0;
}
