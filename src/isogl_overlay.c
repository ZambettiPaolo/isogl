#include "isogl_overlay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERLAY_PATH_MAX 4096

/* 40-byte overlay vertex: position, colour, clip rect in top-left pixels. */
typedef struct {
    float x, y;
    float r, g, b, a;
    float clip_x0, clip_y0, clip_x1, clip_y1;
} isogl_overlay_vertex;

typedef struct {
    float x, y, w, h;
    float rgba[4];
} overlay_quad_cmd;

typedef struct {
    float x0, y0, x1, y1;
    float rgba[4];
} overlay_line_cmd;

typedef struct {
    char *text;
    float x, y;
    float height;
    float angle;
    float rgba[4];
} overlay_text_cmd;

typedef struct {
    isogl_overlay_group id;
    int alive;
    int created;

    overlay_quad_cmd *quads;
    size_t quad_count, quad_capacity;
    overlay_line_cmd *lines;
    size_t line_count, line_capacity;
    overlay_text_cmd *texts;
    size_t text_count, text_capacity;

    float tx, ty, scale;
    int layer;
    int visible;
    int clip_enabled;
    float clip[4];          /* viewport-local top-left x, y, w, h */
    int has_color;
    float color[4];

    isogl_string_list strings;

    size_t quad_first, quad_vertices;
    size_t line_first, line_vertices;

    int geom_dirty;
    int text_rebuild;
    int text_move;
    int text_color;
    int text_clip;
} overlay_group;

struct isogl_overlay {
    isogl_font_data *font;

    overlay_group *groups;
    size_t group_count, group_capacity;
    isogl_overlay_group next_id;
    int next_created;

    size_t *order;          /* indices into groups, sorted by layer/created */
    size_t order_count, order_capacity;
    int order_invalid;

    isogl_overlay_vertex *quad_vertices;
    size_t quad_count, quad_capacity;
    isogl_overlay_vertex *line_vertices;
    size_t line_count, line_capacity;

    GLuint program;
    GLint u_view;
    GLuint quad_vao, quad_vbo;
    GLuint line_vao, line_vbo;

    int initialized;
    int failed;
    int geom_invalid;
    int last_viewport_height;
    isogl_overlay_stats stats;
};

/* ---- small helpers ------------------------------------------------------ */

static overlay_group *overlay_find(isogl_overlay *o, isogl_overlay_group id) {
    if (!o || id == ISOGL_OVERLAY_GROUP_INVALID) return NULL;
    for (size_t i = 0; i < o->group_count; ++i) {
        if (o->groups[i].alive && o->groups[i].id == id) {
            return &o->groups[i];
        }
    }
    return NULL;
}

static int overlay_grow(void **ptr, size_t *capacity, size_t needed,
                        size_t element_size) {
    if (needed <= *capacity) return 0;

    size_t new_capacity = *capacity ? *capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > ((size_t)-1) / 2) return -1;
        new_capacity *= 2;
    }
    if (new_capacity > ((size_t)-1) / element_size) return -1;

    void *grown = realloc(*ptr, new_capacity * element_size);
    if (!grown) return -1;
    *ptr = grown;
    *capacity = new_capacity;
    return 0;
}

static void overlay_mark_geometry(isogl_overlay *o, overlay_group *g) {
    g->geom_dirty = 1;
    o->geom_invalid = 1;
}

static void overlay_mark_order(isogl_overlay *o) {
    o->order_invalid = 1;
    o->geom_invalid = 1;
}

static void overlay_group_free_text_cmds(overlay_group *g) {
    for (size_t i = 0; i < g->text_count; ++i) {
        free(g->texts[i].text);
    }
    free(g->texts);
    g->texts = NULL;
    g->text_count = 0;
    g->text_capacity = 0;
}

static void overlay_group_free_contents(overlay_group *g) {
    free(g->quads);
    g->quads = NULL;
    g->quad_count = 0;
    g->quad_capacity = 0;

    free(g->lines);
    g->lines = NULL;
    g->line_count = 0;
    g->line_capacity = 0;

    overlay_group_free_text_cmds(g);

    isogl_string_list_free(&g->strings);
    memset(&g->strings, 0, sizeof(g->strings));

    g->quad_first = 0;
    g->quad_vertices = 0;
    g->line_first = 0;
    g->line_vertices = 0;
}

static void overlay_vertex_set(isogl_overlay_vertex *v,
                               float x, float y, const float rgba[4],
                               const float clip[4]) {
    v->x = x;
    v->y = y;
    v->r = rgba[0];
    v->g = rgba[1];
    v->b = rgba[2];
    v->a = rgba[3];
    v->clip_x0 = clip[0];
    v->clip_y0 = clip[1];
    v->clip_x1 = clip[2];
    v->clip_y1 = clip[3];
}

static int overlay_push_vertex(isogl_overlay_vertex **array, size_t *count,
                               size_t *capacity, const isogl_overlay_vertex *v) {
    if (overlay_grow((void **)array, capacity, *count + 1,
                     sizeof(isogl_overlay_vertex)) != 0) {
        return -1;
    }
    (*array)[(*count)++] = *v;
    return 0;
}

static void overlay_group_clip(const overlay_group *g, float clip[4]) {
    if (g->clip_enabled) {
        clip[0] = g->clip[0];
        clip[1] = g->clip[1];
        clip[2] = g->clip[0] + g->clip[2];
        clip[3] = g->clip[1] + g->clip[3];
    } else {
        clip[0] = -1.0e9f;
        clip[1] = -1.0e9f;
        clip[2] = 1.0e9f;
        clip[3] = 1.0e9f;
    }
}

static int overlay_group_less(const isogl_overlay *o, size_t a, size_t b) {
    const overlay_group *ga = &o->groups[a];
    const overlay_group *gb = &o->groups[b];
    if (ga->layer != gb->layer) return ga->layer < gb->layer;
    return ga->created < gb->created;
}

static void overlay_rebuild_order(isogl_overlay *o) {
    o->order_count = 0;
    if (overlay_grow((void **)&o->order, &o->order_capacity,
                     o->group_count ? o->group_count : 1,
                     sizeof(size_t)) != 0) {
        o->order_invalid = 0;
        return;
    }

    for (size_t i = 0; i < o->group_count; ++i) {
        if (o->groups[i].alive) {
            o->order[o->order_count++] = i;
        }
    }

    /* Insertion sort: the UI has a handful of groups and this runs only when
       the order actually changes. */
    for (size_t i = 1; i < o->order_count; ++i) {
        size_t key = o->order[i];
        size_t j = i;
        while (j > 0 && overlay_group_less(o, key, o->order[j - 1])) {
            o->order[j] = o->order[j - 1];
            --j;
        }
        o->order[j] = key;
    }

    o->order_invalid = 0;
}

/* ---- text --------------------------------------------------------------- */

static void overlay_text_placement(const isogl_overlay *o,
                                   const overlay_group *g,
                                   const overlay_text_cmd *cmd,
                                   int viewport_height,
                                   float *x, float *y, float *height) {
    (void)o;
    float top_x = g->tx + cmd->x * g->scale;
    float top_y = g->ty + cmd->y * g->scale;
    *x = top_x;
    *y = (float)viewport_height - top_y;
    *height = cmd->height * g->scale;
    if (*height < 0.0f) *height = -*height;
}

static void overlay_text_apply_color(overlay_group *g, size_t index) {
    const float *color = NULL;
    if (g->has_color) {
        color = g->color;
    } else if (index < g->text_count) {
        color = g->texts[index].rgba;
    }
    isogl_glyph_string_set_color(&g->strings.glyph_strings[index], color);
}

static void overlay_text_apply_clip(overlay_group *g, size_t index) {
    if (g->clip_enabled) {
        isogl_glyph_string_set_clip(&g->strings.glyph_strings[index], 1,
                                    g->clip);
    } else {
        isogl_glyph_string_set_clip(&g->strings.glyph_strings[index], 0, NULL);
    }
}

static void overlay_group_rebuild_texts(isogl_overlay *o, overlay_group *g,
                                        int viewport_height) {
    isogl_string_list_free(&g->strings);
    memset(&g->strings, 0, sizeof(g->strings));

    if (o->font) {
        for (size_t i = 0; i < g->text_count; ++i) {
            overlay_text_cmd *cmd = &g->texts[i];
            isogl_glyph_string *string =
                isogl_create_glyph_string(cmd->text, o->font);
            if (!string) continue;

            const float *color = g->has_color ? g->color : cmd->rgba;
            isogl_glyph_string_set_color(string, color);
            if (g->clip_enabled) {
                isogl_glyph_string_set_clip(string, 1, g->clip);
            }

            float x, y, height;
            overlay_text_placement(o, g, cmd, viewport_height,
                                   &x, &y, &height);
            if (!isogl_string_list_append(&g->strings, string, x, y, height,
                                          1.0f, cmd->angle, o->font, 0)) {
                isogl_free_glyph_string(string);
            }
        }
    }

    g->text_rebuild = 0;
    g->text_move = 0;
    g->text_color = 0;
    g->text_clip = 0;
}

static void overlay_group_update_text(isogl_overlay *o, overlay_group *g,
                                      int viewport_height) {
    if (!o->font) {
        isogl_string_list_free(&g->strings);
        memset(&g->strings, 0, sizeof(g->strings));
        g->text_rebuild = 0;
        g->text_move = 0;
        g->text_color = 0;
        g->text_clip = 0;
        return;
    }

    if (g->text_rebuild ||
        g->strings.glyph_string_count != g->text_count) {
        overlay_group_rebuild_texts(o, g, viewport_height);
        return;
    }

    if (g->text_move) {
        for (size_t i = 0; i < g->text_count; ++i) {
            float x, y, height;
            overlay_text_placement(o, g, &g->texts[i], viewport_height,
                                   &x, &y, &height);
            isogl_string_list_set_transform_by_index(&g->strings, (uint32_t)i,
                                                     o->font, x, y, height,
                                                     1.0f,
                                                     g->texts[i].angle);
        }
        g->text_move = 0;
    }

    if (g->text_color) {
        for (size_t i = 0; i < g->strings.glyph_string_count; ++i) {
            overlay_text_apply_color(g, i);
        }
        g->text_color = 0;
    }

    if (g->text_clip) {
        for (size_t i = 0; i < g->strings.glyph_string_count; ++i) {
            overlay_text_apply_clip(g, i);
        }
        g->text_clip = 0;
    }
}

/* ---- geometry baking ---------------------------------------------------- */

static int overlay_bake_group(isogl_overlay *o, overlay_group *g) {
    float clip[4];
    overlay_group_clip(g, clip);

    g->quad_first = o->quad_count;
    for (size_t i = 0; i < g->quad_count; ++i) {
        const overlay_quad_cmd *q = &g->quads[i];
        const float *color = g->has_color ? g->color : q->rgba;
        float x0 = g->tx + q->x * g->scale;
        float y0 = g->ty + q->y * g->scale;
        float x1 = x0 + q->w * g->scale;
        float y1 = y0 + q->h * g->scale;

        isogl_overlay_vertex v;
        overlay_vertex_set(&v, x0, y0, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x1, y0, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x1, y1, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x0, y0, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x1, y1, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x0, y1, color, clip);
        if (overlay_push_vertex(&o->quad_vertices, &o->quad_count,
                                &o->quad_capacity, &v) != 0) return -1;
    }
    g->quad_vertices = o->quad_count - g->quad_first;

    g->line_first = o->line_count;
    for (size_t i = 0; i < g->line_count; ++i) {
        const overlay_line_cmd *l = &g->lines[i];
        const float *color = g->has_color ? g->color : l->rgba;
        float x0 = g->tx + l->x0 * g->scale;
        float y0 = g->ty + l->y0 * g->scale;
        float x1 = g->tx + l->x1 * g->scale;
        float y1 = g->ty + l->y1 * g->scale;

        isogl_overlay_vertex v;
        overlay_vertex_set(&v, x0, y0, color, clip);
        if (overlay_push_vertex(&o->line_vertices, &o->line_count,
                                &o->line_capacity, &v) != 0) return -1;
        overlay_vertex_set(&v, x1, y1, color, clip);
        if (overlay_push_vertex(&o->line_vertices, &o->line_count,
                                &o->line_capacity, &v) != 0) return -1;
    }
    g->line_vertices = o->line_count - g->line_first;

    g->geom_dirty = 0;
    return 0;
}

static int overlay_rebuild_geometry(isogl_overlay *o) {
    o->quad_count = 0;
    o->line_count = 0;

    for (size_t i = 0; i < o->order_count; ++i) {
        overlay_group *g = &o->groups[o->order[i]];
        if (!g->alive) continue;
        if (!g->visible) {
            g->quad_first = o->quad_count;
            g->quad_vertices = 0;
            g->line_first = o->line_count;
            g->line_vertices = 0;
            g->geom_dirty = 0;
            continue;
        }
        if (overlay_bake_group(o, g) != 0) return -1;
    }
    return 0;
}

static int overlay_upload_geometry(isogl_overlay *o) {
    if (o->quad_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, o->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(o->quad_count * sizeof(isogl_overlay_vertex)),
                     o->quad_count ? o->quad_vertices : NULL,
                     GL_DYNAMIC_DRAW);
    }
    if (o->line_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, o->line_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(o->line_count * sizeof(isogl_overlay_vertex)),
                     o->line_count ? o->line_vertices : NULL,
                     GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

/* ---- GL resources ------------------------------------------------------- */

static void overlay_configure_vao(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(isogl_overlay_vertex),
                          (void *)offsetof(isogl_overlay_vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          sizeof(isogl_overlay_vertex),
                          (void *)offsetof(isogl_overlay_vertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                          sizeof(isogl_overlay_vertex),
                          (void *)offsetof(isogl_overlay_vertex, clip_x0));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static GLuint overlay_load_program(void) {
    char vs_path[OVERLAY_PATH_MAX];
    char fs_path[OVERLAY_PATH_MAX];

    if (isogl_asset_path("shaders/vs_overlay.glsl", vs_path,
                         sizeof(vs_path)) != 0) {
        return 0;
    }
    if (isogl_asset_path("shaders/fs_overlay.glsl", fs_path,
                         sizeof(fs_path)) != 0) {
        return 0;
    }
    return isogl_create_shader_program(vs_path, NULL, fs_path);
}

/* ---- public API --------------------------------------------------------- */

isogl_overlay *isogl_overlay_create(isogl_font_data *font) {
    isogl_overlay *o = calloc(1, sizeof(isogl_overlay));
    if (!o) return NULL;

    o->font = font;
    o->next_id = 1;
    o->last_viewport_height = -1;
    o->order_invalid = 1;

    o->program = overlay_load_program();
    if (o->program == 0) {
        free(o);
        return NULL;
    }
    o->u_view = glGetUniformLocation(o->program, "uView");

    glGenVertexArrays(1, &o->quad_vao);
    glGenBuffers(1, &o->quad_vbo);
    glGenVertexArrays(1, &o->line_vao);
    glGenBuffers(1, &o->line_vbo);
    if (o->quad_vao == 0 || o->quad_vbo == 0 ||
        o->line_vao == 0 || o->line_vbo == 0) {
        isogl_overlay_destroy(o);
        return NULL;
    }

    overlay_configure_vao(o->quad_vao, o->quad_vbo);
    overlay_configure_vao(o->line_vao, o->line_vbo);

    o->initialized = 1;
    return o;
}

void isogl_overlay_destroy(isogl_overlay *o) {
    if (!o) return;

    for (size_t i = 0; i < o->group_count; ++i) {
        overlay_group_free_contents(&o->groups[i]);
    }
    free(o->groups);
    free(o->order);
    free(o->quad_vertices);
    free(o->line_vertices);

    if (o->quad_vbo) glDeleteBuffers(1, &o->quad_vbo);
    if (o->line_vbo) glDeleteBuffers(1, &o->line_vbo);
    if (o->quad_vao) glDeleteVertexArrays(1, &o->quad_vao);
    if (o->line_vao) glDeleteVertexArrays(1, &o->line_vao);
    if (o->program) glDeleteProgram(o->program);

    free(o);
}

void isogl_overlay_set_font(isogl_overlay *o, isogl_font_data *font) {
    if (!o) return;
    o->font = font;
    for (size_t i = 0; i < o->group_count; ++i) {
        if (o->groups[i].alive && o->groups[i].text_count > 0) {
            o->groups[i].text_rebuild = 1;
        }
    }
}

isogl_overlay_group isogl_overlay_group_create(isogl_overlay *o, int layer) {
    if (!o || !o->initialized) return ISOGL_OVERLAY_GROUP_INVALID;

    size_t slot = o->group_count;
    for (size_t i = 0; i < o->group_count; ++i) {
        if (!o->groups[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot == o->group_count) {
        if (overlay_grow((void **)&o->groups, &o->group_capacity,
                         o->group_count + 1, sizeof(overlay_group)) != 0) {
            return ISOGL_OVERLAY_GROUP_INVALID;
        }
        o->group_count++;
    }

    overlay_group *g = &o->groups[slot];
    memset(g, 0, sizeof(*g));
    g->id = o->next_id++;
    g->alive = 1;
    g->created = o->next_created++;
    g->layer = layer;
    g->visible = 1;
    g->scale = 1.0f;
    g->geom_dirty = 1;

    overlay_mark_order(o);
    return g->id;
}

void isogl_overlay_group_destroy(isogl_overlay *o, isogl_overlay_group group) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;

    overlay_group_free_contents(g);
    memset(g, 0, sizeof(*g));
    g->alive = 0;
    overlay_mark_order(o);
}

void isogl_overlay_group_clear(isogl_overlay *o, isogl_overlay_group group) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;

    overlay_group_free_contents(g);
    g->geom_dirty = 1;
    g->text_rebuild = 1;
    o->geom_invalid = 1;
}

void isogl_overlay_group_quad(isogl_overlay *o, isogl_overlay_group group,
                              float x, float y, float w, float h,
                              const float rgba[4]) {
    overlay_group *g = overlay_find(o, group);
    if (!g || !rgba) return;

    if (overlay_grow((void **)&g->quads, &g->quad_capacity,
                     g->quad_count + 1, sizeof(overlay_quad_cmd)) != 0) {
        return;
    }
    overlay_quad_cmd *q = &g->quads[g->quad_count++];
    q->x = x;
    q->y = y;
    q->w = w;
    q->h = h;
    memcpy(q->rgba, rgba, sizeof(q->rgba));
    overlay_mark_geometry(o, g);
}

void isogl_overlay_group_line(isogl_overlay *o, isogl_overlay_group group,
                              float x0, float y0, float x1, float y1,
                              const float rgba[4]) {
    overlay_group *g = overlay_find(o, group);
    if (!g || !rgba) return;

    if (overlay_grow((void **)&g->lines, &g->line_capacity,
                     g->line_count + 1, sizeof(overlay_line_cmd)) != 0) {
        return;
    }
    overlay_line_cmd *l = &g->lines[g->line_count++];
    l->x0 = x0;
    l->y0 = y0;
    l->x1 = x1;
    l->y1 = y1;
    memcpy(l->rgba, rgba, sizeof(l->rgba));
    overlay_mark_geometry(o, g);
}

void isogl_overlay_group_text(isogl_overlay *o, isogl_overlay_group group,
                              const char *utf8, float x, float baseline_y,
                              float height_px, float angle,
                              const float rgba[4]) {
    overlay_group *g = overlay_find(o, group);
    if (!g || !utf8 || !rgba) return;

    if (overlay_grow((void **)&g->texts, &g->text_capacity,
                     g->text_count + 1, sizeof(overlay_text_cmd)) != 0) {
        return;
    }

    size_t length = strlen(utf8);
    char *copy = malloc(length + 1);
    if (!copy) return;
    memcpy(copy, utf8, length + 1);

    overlay_text_cmd *t = &g->texts[g->text_count++];
    t->text = copy;
    t->x = x;
    t->y = baseline_y;
    t->height = height_px;
    t->angle = angle;
    memcpy(t->rgba, rgba, sizeof(t->rgba));

    g->text_rebuild = 1;
}

void isogl_overlay_group_set_transform(isogl_overlay *o,
                                       isogl_overlay_group group,
                                       float tx, float ty, float scale) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;
    if (g->tx == tx && g->ty == ty && g->scale == scale) return;

    g->tx = tx;
    g->ty = ty;
    g->scale = scale;
    overlay_mark_geometry(o, g);
    g->text_move = 1;
}

void isogl_overlay_group_set_layer(isogl_overlay *o, isogl_overlay_group group,
                                   int layer) {
    overlay_group *g = overlay_find(o, group);
    if (!g || g->layer == layer) return;

    g->layer = layer;
    overlay_mark_order(o);
}

void isogl_overlay_group_set_clip(isogl_overlay *o, isogl_overlay_group group,
                                  int enabled, float x, float y,
                                  float w, float h) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;

    int on = enabled && w > 0.0f && h > 0.0f;
    if (g->clip_enabled == on &&
        (!on || (g->clip[0] == x && g->clip[1] == y &&
                 g->clip[2] == w && g->clip[3] == h))) {
        return;
    }

    g->clip_enabled = on;
    g->clip[0] = x;
    g->clip[1] = y;
    g->clip[2] = w;
    g->clip[3] = h;
    overlay_mark_geometry(o, g);
    g->text_clip = 1;
}

void isogl_overlay_group_set_color(isogl_overlay *o, isogl_overlay_group group,
                                   const float rgba[4]) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;

    if (!rgba) {
        if (!g->has_color) return;
        g->has_color = 0;
    } else {
        if (g->has_color &&
            memcmp(g->color, rgba, sizeof(g->color)) == 0) {
            return;
        }
        memcpy(g->color, rgba, sizeof(g->color));
        g->has_color = 1;
    }
    overlay_mark_geometry(o, g);
    g->text_color = 1;
}

void isogl_overlay_group_set_visible(isogl_overlay *o,
                                     isogl_overlay_group group, int visible) {
    overlay_group *g = overlay_find(o, group);
    if (!g) return;

    int on = visible ? 1 : 0;
    if (g->visible == on) return;
    g->visible = on;
    overlay_mark_order(o);
}

int isogl_overlay_commit(isogl_overlay *o) {
    if (!o || !o->initialized || o->failed) return -1;

    isogl_viewport vp;
    int have_vp = (isogl_get_viewport(&vp) == 0);
    if (have_vp && vp.height != o->last_viewport_height) {
        o->last_viewport_height = vp.height;
        for (size_t i = 0; i < o->group_count; ++i) {
            overlay_group *g = &o->groups[i];
            if (!g->alive || g->text_count == 0) continue;
            if (g->strings.glyph_string_count != g->text_count) {
                g->text_rebuild = 1;
            } else {
                g->text_move = 1;
            }
        }
    }

    if (o->order_invalid) {
        overlay_rebuild_order(o);
        o->geom_invalid = 1;
    }

    if (o->geom_invalid) {
        if (overlay_rebuild_geometry(o) != 0) {
            o->failed = 1;
            return -1;
        }
        overlay_upload_geometry(o);
        o->geom_invalid = 0;
    }

    if (have_vp) {
        for (size_t i = 0; i < o->group_count; ++i) {
            overlay_group *g = &o->groups[i];
            if (!g->alive) continue;
            if (g->text_rebuild || g->text_move ||
                g->text_color || g->text_clip) {
                overlay_group_update_text(o, g, vp.height);
            }
        }
    }

    o->stats.groups = 0;
    o->stats.quads = o->quad_count / 6;
    o->stats.lines = o->line_count / 2;
    o->stats.texts = 0;
    for (size_t i = 0; i < o->group_count; ++i) {
        if (!o->groups[i].alive) continue;
        o->stats.groups++;
        if (o->groups[i].visible) {
            o->stats.texts += o->groups[i].text_count;
        }
    }
    return 0;
}

/* ---- drawing ------------------------------------------------------------ */

static void overlay_draw_text_run(isogl_overlay *o, size_t begin, size_t end,
                                  const float identity[16],
                                  const float text_view[16]) {
    if (!isogl_shader_program_font) return;

    for (size_t k = begin; k < end; ++k) {
        overlay_group *g = &o->groups[o->order[k]];
        if (!g->alive || !g->visible) continue;
        if (g->strings.glyph_string_count == 0) continue;

        isogl_draw_string_list_ex(&g->strings, isogl_shader_program_font,
                                  (float *)identity, (float *)text_view,
                                  (float *)identity, 0);
        for (uint32_t s = 0; s < g->strings.glyph_string_count; ++s) {
            if (g->strings.glyph_strings[s].vertex_count > 0 &&
                g->strings.glyph_strings[s].vbo != 0) {
                o->stats.draw_calls++;
            }
        }
    }
}

int isogl_overlay_draw(isogl_overlay *o) {
    if (!o || !o->initialized || o->failed) return -1;

    isogl_viewport vp;
    if (isogl_get_viewport(&vp) != 0) return -1;
    if (isogl_overlay_commit(o) != 0) return -1;

    o->stats.draw_calls = 0;

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    float geom[16] = {0};
    geom[0] = 2.0f / (float)vp.width;
    geom[5] = -2.0f / (float)vp.height;
    geom[10] = 1.0f;
    geom[12] = -1.0f;
    geom[13] = 1.0f;
    geom[15] = 1.0f;

    float text[16] = {0};
    text[0] = 2.0f / (float)vp.width;
    text[5] = 2.0f / (float)vp.height;
    text[10] = 1.0f;
    text[12] = -1.0f;
    text[13] = -1.0f;
    text[15] = 1.0f;

    size_t i = 0;
    while (i < o->order_count) {
        int layer = o->groups[o->order[i]].layer;
        size_t j = i + 1;
        while (j < o->order_count &&
               o->groups[o->order[j]].layer == layer) {
            ++j;
        }

        size_t quad_first = 0, quad_end = 0;
        size_t line_first = 0, line_end = 0;
        int have_quad = 0, have_line = 0;

        for (size_t k = i; k < j; ++k) {
            overlay_group *g = &o->groups[o->order[k]];
            if (!g->alive || !g->visible) continue;
            if (g->quad_vertices > 0) {
                if (!have_quad) {
                    quad_first = g->quad_first;
                    have_quad = 1;
                }
                quad_end = g->quad_first + g->quad_vertices;
            }
            if (g->line_vertices > 0) {
                if (!have_line) {
                    line_first = g->line_first;
                    have_line = 1;
                }
                line_end = g->line_first + g->line_vertices;
            }
        }

        if (o->program) {
            glUseProgram(o->program);
            if (o->u_view != -1) {
                glUniformMatrix4fv(o->u_view, 1, GL_FALSE, geom);
            }

            if (have_quad) {
                glBindVertexArray(o->quad_vao);
                glDrawArrays(GL_TRIANGLES, (GLint)quad_first,
                             (GLsizei)(quad_end - quad_first));
                o->stats.draw_calls++;
            }
            if (have_line) {
                glBindVertexArray(o->line_vao);
                glDrawArrays(GL_LINES, (GLint)line_first,
                             (GLsizei)(line_end - line_first));
                o->stats.draw_calls++;
            }
            glBindVertexArray(0);
            glUseProgram(0);
        }

        overlay_draw_text_run(o, i, j, identity, text);

        i = j;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    return 0;
}

isogl_overlay_stats isogl_overlay_get_stats(const isogl_overlay *o) {
    isogl_overlay_stats empty = {0, 0, 0, 0, 0};
    if (!o) return empty;
    return o->stats;
}
