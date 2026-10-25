#include "cad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAD_CIRCLE_SEGMENTS 96

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static CadEntity *cad_push_entity(CadDocument *doc, CadEntityType type, int layer,
                                  isogl_line_type pattern, isogl_line_weight weight) {
    if (doc->count == doc->capacity) {
        size_t capacity = doc->capacity ? doc->capacity * 2 : 32;
        CadEntity *grown = realloc(doc->entities, capacity * sizeof(CadEntity));
        if (!grown) return NULL;
        doc->entities = grown;
        doc->capacity = capacity;
    }

    CadEntity *entity = &doc->entities[doc->count];
    memset(entity, 0, sizeof(*entity));
    entity->id = (uint32_t)doc->count + 1;
    entity->type = type;
    if (layer < 0) layer = 0;
    if (doc->layer_count > 0 && layer >= doc->layer_count) layer = doc->layer_count - 1;
    entity->layer = (uint8_t)layer;
    entity->pattern = pattern;
    entity->weight = weight;
    entity->visible = 1;
    doc->count++;
    return entity;
}

static isogl_vertex cad_vertex(const CadDocument *doc, const CadEntity *entity,
                               float x, float y, float distance) {
    isogl_vertex v;
    memset(&v, 0, sizeof(v));
    v.position[0] = x;
    v.position[1] = y;

    const CadLayer *layer = &doc->layers[entity->layer];
    memcpy(v.color, layer->color, sizeof(v.color));

    v.distanceFromStart = distance;
    v.pk_thickness_pattern_layer =
        isogl_pack((uint8_t)entity->weight, (uint8_t)entity->pattern, entity->layer);
    return v;
}

static int cad_upload_entity(CadDocument *doc, CadEntity *entity,
                             isogl_vertex *vertices, size_t vertex_count) {
    if (vertex_count == 0) return -1;

    isogl_segment_list list;
    if (isogl_segment_list_init(&list, vertex_count) != 0) return -1;

    for (size_t i = 0; i + 1 < vertex_count; i += 2) {
        if (isogl_segment_list_push_pair(&list, vertices[i], vertices[i + 1]) != 0) {
            isogl_segment_list_free(&list);
            return -1;
        }
    }

    GLuint id = isogl_vbo_array_add(&doc->parts, &list, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&list);

    if (id == 0) return -1;
    entity->vbo_id = id;
    return 0;
}

int cad_document_init(CadDocument *doc, isogl_font_data *font, float sheet_scale) {
    if (!doc) return -1;
    memset(doc, 0, sizeof(*doc));
    doc->font = font;
    doc->sheet_scale = sheet_scale > 0.0f ? sheet_scale : 1.0f;

    if (isogl_vbo_array_init(&doc->parts) != 0) return -1;

    doc->layer_count = 4;
    snprintf(doc->layers[0].name, sizeof(doc->layers[0].name), "Contorni");
    doc->layers[0].color[0] = 1.0f;
    doc->layers[0].color[1] = 1.0f;
    doc->layers[0].color[2] = 1.0f;
    doc->layers[0].color[3] = 1.0f;
    doc->layers[0].default_weight = LINE_THICKNESS_0_5;
    doc->layers[0].default_pattern = LINE_TYPE_CONTINUOUS;

    snprintf(doc->layers[1].name, sizeof(doc->layers[1].name), "Assi");
    doc->layers[1].color[0] = 0.2f;
    doc->layers[1].color[1] = 0.9f;
    doc->layers[1].color[2] = 0.2f;
    doc->layers[1].color[3] = 1.0f;
    doc->layers[1].default_weight = LINE_THICKNESS_0_18;
    doc->layers[1].default_pattern = LINE_TYPE_LONG_DASH_DOT;

    snprintf(doc->layers[2].name, sizeof(doc->layers[2].name), "Quote");
    doc->layers[2].color[0] = 1.0f;
    doc->layers[2].color[1] = 0.85f;
    doc->layers[2].color[2] = 0.2f;
    doc->layers[2].color[3] = 1.0f;
    doc->layers[2].default_weight = LINE_THICKNESS_0_18;
    doc->layers[2].default_pattern = LINE_TYPE_CONTINUOUS;

    snprintf(doc->layers[3].name, sizeof(doc->layers[3].name), "Testo");
    doc->layers[3].color[0] = 1.0f;
    doc->layers[3].color[1] = 1.0f;
    doc->layers[3].color[2] = 1.0f;
    doc->layers[3].color[3] = 1.0f;
    doc->layers[3].default_weight = LINE_THICKNESS_0_25;
    doc->layers[3].default_pattern = LINE_TYPE_CONTINUOUS;

    for (int i = 0; i < doc->layer_count; ++i) doc->layers[i].visible = 1;
    return 0;
}

void cad_document_free(CadDocument *doc) {
    if (!doc) return;
    isogl_string_list_free(&doc->strings);
    isogl_vbo_array_free(&doc->parts);
    free(doc->entities);
    doc->entities = NULL;
    doc->count = 0;
    doc->capacity = 0;
}

uint32_t cad_add_line(CadDocument *doc, int layer, isogl_line_type pattern,
                      isogl_line_weight weight, float x0, float y0,
                      float x1, float y1) {
    CadEntity *entity = cad_push_entity(doc, CAD_LINE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.line.x0 = x0;
    entity->geom.line.y0 = y0;
    entity->geom.line.x1 = x1;
    entity->geom.line.y1 = y1;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);

    isogl_vertex pair[2] = {
        cad_vertex(doc, entity, x0, y0, 0.0f),
        cad_vertex(doc, entity, x1, y1, length)
    };

    cad_upload_entity(doc, entity, pair, 2);
    return entity->id;
}

uint32_t cad_add_circle(CadDocument *doc, int layer, isogl_line_type pattern,
                        isogl_line_weight weight, float cx, float cy, float r) {
    CadEntity *entity = cad_push_entity(doc, CAD_CIRCLE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.circle.cx = cx;
    entity->geom.circle.cy = cy;
    entity->geom.circle.r = r;

    isogl_vertex vertices[CAD_CIRCLE_SEGMENTS * 2];
    float px = cx + r;
    float py = cy;
    float distance = 0.0f;

    for (int i = 0; i < CAD_CIRCLE_SEGMENTS; ++i) {
        float angle = (float)(i + 1) * (2.0f * (float)M_PI / CAD_CIRCLE_SEGMENTS);
        float nx = cx + r * cosf(angle);
        float ny = cy + r * sinf(angle);
        float dx = nx - px;
        float dy = ny - py;

        vertices[i * 2 + 0] = cad_vertex(doc, entity, px, py, distance);
        distance += sqrtf(dx * dx + dy * dy);
        vertices[i * 2 + 1] = cad_vertex(doc, entity, nx, ny, distance);

        px = nx;
        py = ny;
    }

    cad_upload_entity(doc, entity, vertices, CAD_CIRCLE_SEGMENTS * 2);
    return entity->id;
}

uint32_t cad_add_rectangle(CadDocument *doc, int layer, isogl_line_type pattern,
                           isogl_line_weight weight, float x0, float y0,
                           float x1, float y1) {
    CadEntity *entity = cad_push_entity(doc, CAD_RECTANGLE, layer, pattern, weight);
    if (!entity) return 0;

    entity->geom.rect.x0 = x0;
    entity->geom.rect.y0 = y0;
    entity->geom.rect.x1 = x1;
    entity->geom.rect.y1 = y1;

    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};

    isogl_vertex vertices[8];
    float distance = 0.0f;

    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) & 3;
        float dx = xs[next] - xs[i];
        float dy = ys[next] - ys[i];

        vertices[i * 2 + 0] = cad_vertex(doc, entity, xs[i], ys[i], distance);
        distance += sqrtf(dx * dx + dy * dy);
        vertices[i * 2 + 1] = cad_vertex(doc, entity, xs[next], ys[next], distance);
    }

    cad_upload_entity(doc, entity, vertices, 8);
    return entity->id;
}

uint32_t cad_add_text(CadDocument *doc, int layer, float x, float y,
                      float height_mm, float angle, const char *text) {
    if (!text) return 0;

    CadEntity *entity = cad_push_entity(doc, CAD_TEXT, layer,
                                        LINE_TYPE_CONTINUOUS, LINE_THICKNESS_0_25);
    if (!entity) return 0;

    entity->geom.label.x = x;
    entity->geom.label.y = y;
    entity->geom.label.height = height_mm;
    entity->geom.label.angle = angle;
    snprintf(entity->geom.label.text, sizeof(entity->geom.label.text), "%s", text);

    cad_document_rebuild_text(doc);
    return entity->id;
}

void cad_document_rebuild_text(CadDocument *doc) {
    if (!doc || !doc->font) return;

    isogl_string_list_free(&doc->strings);

    for (size_t i = 0; i < doc->count; ++i) {
        CadEntity *entity = &doc->entities[i];
        if (entity->type != CAD_TEXT) continue;

        isogl_glyph_string *string =
            isogl_create_glyph_string(entity->geom.label.text, doc->font);
        if (!string) continue;

        isogl_string_list_append(&doc->strings, string,
                                 entity->geom.label.x, entity->geom.label.y,
                                 entity->geom.label.height, doc->sheet_scale,
                                 entity->geom.label.angle, doc->font);
    }
}
