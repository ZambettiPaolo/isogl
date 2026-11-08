#include "isogl_symbol.h"
#include "isogl_font.h"
#include "isogl_symbol_builtins.h"
#include "isogl_symbol_enums.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- model -------------------------------------------------------------- */

typedef struct {
    char *utf8;
    double x, y, height_mm, angle;
    int align;
    float color[4];
} SymTextItem;

typedef struct {
    isogl_segment_list segments;
    SymTextItem *texts;
    size_t text_count, text_capacity;
    IsoglSymPickItem *picks;
    size_t pick_count, pick_capacity;
} SymBuild;

typedef struct {
    int alive;
    int visible;
    int selected;
    const isogl_symbol_type *type;
    isogl_symbol_params params;
    double sheet_scale;
    GLuint vbo_id;
    IsoglSymPickItem *picks;
    size_t pick_count;
} IsoglSymbol;

static IsoglSymbol *symbols = NULL;
static size_t symbol_slots = 0;
static size_t symbol_capacity = 0;
static uint32_t next_symbol_id = 1;
static isogl_vbo_array symbol_parts;
static isogl_string_list symbol_strings;
static isogl_font_data *symbol_font = NULL;
static int symbol_initialized = 0;
/* Module-wide sheet scale.  Authoritative over per-symbol create arguments;
 * see isogl_symbol_set_sheet_scale(). */
static double symbol_sheet_scale = 1.0;

static const isogl_symbol_type **symbol_types = NULL;
static size_t symbol_type_count = 0;
static size_t symbol_type_capacity = 0;
static int builtins_registered = 0;

static char *sym_strdup(const char *text) {
    if (!text) text = "";
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static IsoglSymbol *sym_get(isogl_symbol_id id) {    if (id == 0 || (size_t)id > symbol_slots) return NULL;
    IsoglSymbol *s = &symbols[id - 1];
    return s->alive ? s : NULL;
}

static int sym_ensure_slot(uint32_t id) {
    if ((size_t)id <= symbol_slots) return 0;

    if ((size_t)id > symbol_capacity) {
        size_t capacity = symbol_capacity ? symbol_capacity : 16;
        while (capacity < (size_t)id) {
            if (capacity > SIZE_MAX / 2) return -1;
            capacity *= 2;
        }
        IsoglSymbol *grown = realloc(symbols, capacity * sizeof(*symbols));
        if (!grown) return -1;
        symbols = grown;
        symbol_capacity = capacity;
    }

    memset(symbols + symbol_slots, 0,
           ((size_t)id - symbol_slots) * sizeof(*symbols));
    symbol_slots = (size_t)id;
    return 0;
}

static IsoglSymbol *sym_new_record(isogl_symbol_id *out_id) {
    uint32_t id = next_symbol_id++;
    if (sym_ensure_slot(id) != 0) return NULL;

    IsoglSymbol *s = &symbols[id - 1];
    memset(s, 0, sizeof(*s));
    s->alive = 1;
    s->visible = 1;
    s->vbo_id = 0;
    *out_id = id;
    return s;
}

/* ---- parameter helpers -------------------------------------------------- */

static int sym_kind_is_list(isogl_symbol_param_kind kind) {
    return kind == ISOGL_SYM_PARAM_POINTS ||
           kind == ISOGL_SYM_PARAM_TEXTS ||
           kind == ISOGL_SYM_PARAM_SEGMENTS ||
           kind == ISOGL_SYM_PARAM_NUMBERS ||
           kind == ISOGL_SYM_PARAM_ENUM_LIST;
}

static void sym_param_free(isogl_symbol_param *p) {
    if (!p || !p->desc) return;
    switch (p->desc->kind) {
        case ISOGL_SYM_PARAM_POINTS:
            free(p->value.points);
            break;
        case ISOGL_SYM_PARAM_TEXTS:
            for (size_t i = 0; i < p->count; ++i) {
                free(p->value.texts[i]);
            }
            free(p->value.texts);
            break;
        case ISOGL_SYM_PARAM_SEGMENTS:
            free(p->value.segments);
            break;
        case ISOGL_SYM_PARAM_NUMBERS:
            free(p->value.numbers);
            break;
        case ISOGL_SYM_PARAM_ENUM_LIST:
            free(p->value.integers);
            break;
        default:
            break;
    }
}

static int sym_param_resize(isogl_symbol_param *p, size_t count) {
    if (!p || !p->desc) return -1;
    if (!sym_kind_is_list(p->desc->kind)) {
        return count == 1 ? 0 : -1;
    }

    switch (p->desc->kind) {
        case ISOGL_SYM_PARAM_POINTS: {
            isogl_symbol_point *items = NULL;
            if (count > 0) {
                items = realloc(p->value.points, count * sizeof(*items));
                if (!items) return -1;
                if (count > p->count) {
                    memset(items + p->count, 0,
                           (count - p->count) * sizeof(*items));
                }
            } else {
                free(p->value.points);
            }
            p->value.points = items;
            break;
        }
        case ISOGL_SYM_PARAM_SEGMENTS: {
            isogl_symbol_segment *items = NULL;
            if (count > 0) {
                items = realloc(p->value.segments, count * sizeof(*items));
                if (!items) return -1;
                if (count > p->count) {
                    memset(items + p->count, 0,
                           (count - p->count) * sizeof(*items));
                }
            } else {
                free(p->value.segments);
            }
            p->value.segments = items;
            break;
        }
        case ISOGL_SYM_PARAM_TEXTS: {
            char **items = NULL;
            if (count > 0) {
                items = calloc(count, sizeof(*items));
                if (!items) return -1;
            }
            size_t keep = count < p->count ? count : p->count;
            for (size_t i = 0; i < keep; ++i) items[i] = p->value.texts[i];
            for (size_t i = count; i < p->count; ++i) free(p->value.texts[i]);
            for (size_t i = keep; i < count; ++i) {
                items[i] = malloc(1);
                if (!items[i]) {
                    for (size_t j = 0; j < count; ++j) free(items[j]);
                    free(items);
                    return -1;
                }
                items[i][0] = '\0';
            }
            free(p->value.texts);
            p->value.texts = items;
            break;
        }
        case ISOGL_SYM_PARAM_NUMBERS: {
            double *items = NULL;
            if (count > 0) {
                items = realloc(p->value.numbers, count * sizeof(*items));
                if (!items) return -1;
                if (count > p->count) {
                    memset(items + p->count, 0,
                           (count - p->count) * sizeof(*items));
                }
            } else {
                free(p->value.numbers);
            }
            p->value.numbers = items;
            break;
        }
        case ISOGL_SYM_PARAM_ENUM_LIST: {
            int *items = NULL;
            if (count > 0) {
                items = realloc(p->value.integers, count * sizeof(*items));
                if (!items) return -1;
                if (count > p->count) {
                    memset(items + p->count, 0,
                           (count - p->count) * sizeof(*items));
                }
            } else {
                free(p->value.integers);
            }
            p->value.integers = items;
            break;
        }
        default:
            return -1;
    }

    p->count = count;
    return 0;
}

static void sym_params_free_contents(isogl_symbol_params *params) {
    if (!params) return;
    if (params->params) {
        for (size_t i = 0; i < params->param_count; ++i) {
            sym_param_free(&params->params[i]);
        }
    }
    free(params->params);
    memset(params, 0, sizeof(*params));
}

static int sym_params_alloc(const isogl_symbol_type *type,
                            isogl_symbol_params *out, double sheet_scale) {
    if (!type || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->param_count = type->param_count;
    out->sheet_scale = sheet_scale > 0.0 ? sheet_scale : 1.0;
    out->params = calloc(type->param_count, sizeof(isogl_symbol_param));
    if (!out->params) return -1;

    for (size_t i = 0; i < type->param_count; ++i) {
        const isogl_symbol_param_desc *desc = &type->params[i];
        isogl_symbol_param *p = &out->params[i];
        p->desc = desc;

        if (sym_kind_is_list(desc->kind)) {
            if (sym_param_resize(p, desc->min_count) != 0) {
                sym_params_free_contents(out);
                return -1;
            }
        } else {
            p->count = 1;
            if (desc->kind == ISOGL_SYM_PARAM_COLOR) {
                p->value.color[0] = 1.0f;
                p->value.color[1] = 1.0f;
                p->value.color[2] = 1.0f;
                p->value.color[3] = 1.0f;
            } else if (desc->kind == ISOGL_SYM_PARAM_SCALE) {
                p->value.number = out->sheet_scale;
            }
        }
    }

    if (type->init_params && type->init_params(out) != 0) {
        sym_params_free_contents(out);
        return -1;
    }
    return 0;
}

static int sym_params_copy(const isogl_symbol_params *src,
                           isogl_symbol_params *out, double sheet_scale) {
    if (!src || !src->type || !out) return -1;
    if (sym_params_alloc(src->type, out, sheet_scale) != 0) return -1;

    for (size_t i = 0; i < src->param_count; ++i) {
        const isogl_symbol_param *s = &src->params[i];
        isogl_symbol_param *d = &out->params[i];
        if (!s->desc || !d->desc || s->desc->kind != d->desc->kind) {
            sym_params_free_contents(out);
            return -1;
        }

        if (sym_kind_is_list(s->desc->kind)) {
            if (sym_param_resize(d, s->count) != 0) {
                sym_params_free_contents(out);
                return -1;
            }
            if (s->desc->kind == ISOGL_SYM_PARAM_POINTS) {
                if (s->count > 0) {
                    memcpy(d->value.points, s->value.points,
                           s->count * sizeof(isogl_symbol_point));
                }
            } else if (s->desc->kind == ISOGL_SYM_PARAM_SEGMENTS) {
                if (s->count > 0) {
                    memcpy(d->value.segments, s->value.segments,
                           s->count * sizeof(isogl_symbol_segment));
                }
            } else if (s->desc->kind == ISOGL_SYM_PARAM_NUMBERS) {
                if (s->count > 0) {
                    memcpy(d->value.numbers, s->value.numbers,
                           s->count * sizeof(double));
                }
            } else if (s->desc->kind == ISOGL_SYM_PARAM_ENUM_LIST) {
                if (s->count > 0) {
                    memcpy(d->value.integers, s->value.integers,
                           s->count * sizeof(int));
                }
            } else {
                for (size_t j = 0; j < s->count; ++j) {
                    free(d->value.texts[j]);
                    d->value.texts[j] =
                        sym_strdup(s->value.texts[j] ? s->value.texts[j] : "");
                    if (!d->value.texts[j]) {
                        sym_params_free_contents(out);
                        return -1;
                    }
                }
            }
        } else if (s->desc->kind == ISOGL_SYM_PARAM_COLOR) {
            memcpy(d->value.color, s->value.color, sizeof(d->value.color));
        } else if (s->desc->kind == ISOGL_SYM_PARAM_NUMBER ||
                   s->desc->kind == ISOGL_SYM_PARAM_SCALE) {
            d->value.number = s->value.number;
        } else if (s->desc->kind == ISOGL_SYM_PARAM_INTEGER ||
                   s->desc->kind == ISOGL_SYM_PARAM_BOOLEAN ||
                   s->desc->kind == ISOGL_SYM_PARAM_ENUM) {
            d->value.integer = s->value.integer;
        }
    }

    out->sheet_scale = sheet_scale > 0.0 ? sheet_scale : out->sheet_scale;
    for (size_t i = 0; i < out->param_count; ++i) {
        if (out->params[i].desc &&
            out->params[i].desc->kind == ISOGL_SYM_PARAM_SCALE) {
            out->params[i].value.number = out->sheet_scale;
        }
    }
    return 0;
}

static int sym_ascii_equal_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int sym_desc_is_enum(const isogl_symbol_param_desc *desc) {
    return desc && (desc->kind == ISOGL_SYM_PARAM_ENUM ||
                    desc->kind == ISOGL_SYM_PARAM_ENUM_LIST);
}

const char *isogl_symbol_param_enum_label(const isogl_symbol_param_desc *desc,
                                          int value) {
    if (!sym_desc_is_enum(desc) || !desc->enum_values) {
        return NULL;
    }
    for (size_t i = 0; i < desc->enum_count; ++i) {
        if (desc->enum_values[i].value == value) {
            return desc->enum_values[i].label;
        }
    }
    return NULL;
}

int isogl_symbol_param_enum_value(const isogl_symbol_param_desc *desc,
                                  const char *name, int *out_value) {
    if (!desc || !name || !out_value || !sym_desc_is_enum(desc) ||
        !desc->enum_values) {
        return -1;
    }
    for (size_t i = 0; i < desc->enum_count; ++i) {
        if (sym_ascii_equal_ci(desc->enum_values[i].name, name) ||
            sym_ascii_equal_ci(desc->enum_values[i].label, name)) {
            *out_value = desc->enum_values[i].value;
            return 0;
        }
    }
    return -1;
}

static int sym_params_validate(const isogl_symbol_type *type,
                               const isogl_symbol_params *params) {
    if (!type || !params || params->type != type ||
        params->param_count != type->param_count || !params->params) {
        return -1;
    }

    for (size_t i = 0; i < type->param_count; ++i) {
        const isogl_symbol_param_desc *desc = &type->params[i];
        const isogl_symbol_param *p = &params->params[i];
        if (p->desc != desc) return -1;

        if (sym_kind_is_list(desc->kind)) {
            if (p->count < desc->min_count) return -1;
            if (desc->max_count != 0 && p->count > desc->max_count) return -1;
            if (desc->kind == ISOGL_SYM_PARAM_POINTS && p->count &&
                !p->value.points) return -1;
            if (desc->kind == ISOGL_SYM_PARAM_SEGMENTS && p->count &&
                !p->value.segments) return -1;
            if (desc->kind == ISOGL_SYM_PARAM_TEXTS && p->count &&
                !p->value.texts) return -1;
            if (desc->kind == ISOGL_SYM_PARAM_NUMBERS && p->count &&
                !p->value.numbers) return -1;
            if (desc->kind == ISOGL_SYM_PARAM_ENUM_LIST && p->count &&
                !p->value.integers) return -1;

            if (desc->kind == ISOGL_SYM_PARAM_NUMBERS) {
                for (size_t j = 0; j < p->count; ++j) {
                    if (!isfinite(p->value.numbers[j])) return -1;
                }
            }
        } else if (p->count != 1) {
            return -1;
        }

        /* The descriptor's enum table is authoritative: a value outside it is
           invalid even if the type's validate() would accept the integer.
           ENUM_LIST validates one element at a time. */
        if (desc->kind == ISOGL_SYM_PARAM_ENUM) {
            if (!desc->enum_values || desc->enum_count == 0) return -1;
            if (!isogl_symbol_param_enum_label(desc, p->value.integer)) {
                return -1;
            }
        } else if (desc->kind == ISOGL_SYM_PARAM_ENUM_LIST) {
            if (!desc->enum_values || desc->enum_count == 0) return -1;
            for (size_t j = 0; j < p->count; ++j) {
                if (!isogl_symbol_param_enum_label(desc,
                                                   p->value.integers[j])) {
                    return -1;
                }
            }
        }
    }

    return type->validate ? type->validate(params) : 0;
}

static int sym_params_set_text_internal(isogl_symbol_params *params,
                                        size_t param_index, size_t element,
                                        const char *utf8) {
    if (!params || param_index >= params->param_count) return -1;
    isogl_symbol_param *p = &params->params[param_index];
    if (!p->desc || p->desc->kind != ISOGL_SYM_PARAM_TEXTS ||
        element >= p->count) {
        return -1;
    }
    char *copy = sym_strdup(utf8 ? utf8 : "");
    if (!copy) return -1;
    free(p->value.texts[element]);
    p->value.texts[element] = copy;
    return 0;
}

int isogl_symbol_params_set_text(isogl_symbol_params *params,
                                 size_t param_index, size_t element,
                                 const char *utf8) {
    return sym_params_set_text_internal(params, param_index, element, utf8);
}

int isogl_symbol_params_set_number(isogl_symbol_params *params,
                                   size_t param_index, size_t element,
                                   double value) {
    if (!params || param_index >= params->param_count) return -1;
    isogl_symbol_param *p = &params->params[param_index];
    if (!p->desc || p->desc->kind != ISOGL_SYM_PARAM_NUMBERS ||
        element >= p->count || !p->value.numbers || !isfinite(value)) {
        return -1;
    }
    p->value.numbers[element] = value;
    return 0;
}

int isogl_symbol_params_set_integer(isogl_symbol_params *params,
                                    size_t param_index, size_t element,
                                    int value) {
    if (!params || param_index >= params->param_count) return -1;
    isogl_symbol_param *p = &params->params[param_index];
    if (!p->desc || p->desc->kind != ISOGL_SYM_PARAM_ENUM_LIST ||
        element >= p->count || !p->value.integers) {
        return -1;
    }
    if (!isogl_symbol_param_enum_label(p->desc, value)) return -1;
    p->value.integers[element] = value;
    return 0;
}

int isogl_symbol_params_resize(isogl_symbol_params *params, size_t param_index,
                               size_t count) {
    if (!params || param_index >= params->param_count) return -1;
    isogl_symbol_param *p = &params->params[param_index];
    if (!p->desc || !sym_kind_is_list(p->desc->kind)) return -1;
    if (count < p->desc->min_count) return -1;
    if (p->desc->max_count != 0 && count > p->desc->max_count) return -1;
    return sym_param_resize(p, count);
}

/* ---- built-in types: dimension and center mark -------------------------- */

enum {
    DIM_POINTS = 0, DIM_TEXTS, DIM_SEGMENTS, DIM_SCALE,
    DIM_DISTANCE, DIM_TEXT_OFFSET, DIM_TEXT_HEIGHT,
    DIM_PRECISION, DIM_PICK_TEXT, DIM_MARK_POINTS,
    DIM_ARROW_LEFT, DIM_ARROW_RIGHT,
    DIM_PATTERN, DIM_WEIGHT, DIM_LAYER, DIM_COLOR,
    DIM_PARAM_COUNT
};

static const isogl_symbol_param_desc DIM_PARAMS[DIM_PARAM_COUNT] = {
    [DIM_POINTS] = {
        "points", "Punti misurati", "P1, P2, P3 del simbolo", "mm",
        ISOGL_SYM_PARAM_POINTS, 3, 3, 1
    },
    [DIM_TEXTS] = {
        "texts", "Testi", "Prefisso e suffisso del valore", "",
        ISOGL_SYM_PARAM_TEXTS, 2, 2, 1
    },
    [DIM_SEGMENTS] = {
        "segments", "Segmenti", "Non usati dalla quota", "",
        ISOGL_SYM_PARAM_SEGMENTS, 0, 0, 0
    },
    [DIM_SCALE] = {
        "scale", "Scala foglio", "Sola lettura; rigenera il simbolo", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    },
    [DIM_DISTANCE] = {
        "distance", "Offset linea", "0 = ricavato da P3", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [DIM_TEXT_OFFSET] = {
        "text_offset_mm", "Offset testo", "Lungo la linea di quota", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [DIM_TEXT_HEIGHT] = {
        "text_height_mm", "Altezza testo", "Altezza ISO su carta", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [DIM_PRECISION] = {
        "precision", "Decimali", "Cifre del valore automatico", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [DIM_PICK_TEXT] = {
        "pick_text", "Testo selezionabile", "Include l'OBB del testo", "",
        ISOGL_SYM_PARAM_BOOLEAN, 1, 1, 1
    },
    [DIM_MARK_POINTS] = {
        "mark_points", "Punti di riferimento", "Segmento da 0.5 mm", "",
        ISOGL_SYM_PARAM_BOOLEAN, 1, 1, 1
    },
    [DIM_ARROW_LEFT] = {
        "arrow_left", "Freccia sinistra", "Stile della punta a P1", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_arrow_values, ISOGL_SYM_ARROW_VALUE_COUNT
    },
    [DIM_ARROW_RIGHT] = {
        "arrow_right", "Freccia destra", "Stile della punta a P2", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_arrow_values, ISOGL_SYM_ARROW_VALUE_COUNT
    },
    [DIM_PATTERN] = {
        "pattern", "Tipo linea ISO", "Pattern della quota", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_type_values, ISOGL_SYM_LINE_TYPE_VALUE_COUNT
    },
    [DIM_WEIGHT] = {
        "weight", "Peso ISO", "Spessore della quota", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_weight_values, ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT
    },
    [DIM_LAYER] = {
        "layer", "Layer", "Layer di stile", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [DIM_COLOR] = {
        "color", "Colore", "Colore di stampa", "",
        ISOGL_SYM_PARAM_COLOR, 1, 1, 1
    }
};

enum {
    CEN_POINTS = 0, CEN_TEXTS, CEN_SEGMENTS, CEN_SCALE,
    CEN_SIZE, CEN_PATTERN, CEN_WEIGHT, CEN_LAYER, CEN_COLOR,
    CEN_PARAM_COUNT
};

static const isogl_symbol_param_desc CEN_PARAMS[CEN_PARAM_COUNT] = {
    [CEN_POINTS] = {
        "points", "Punto centrale", "Centro del center mark", "mm",
        ISOGL_SYM_PARAM_POINTS, 1, 1, 1
    },
    [CEN_TEXTS] = {
        "texts", "Testi", "Non usati dal center mark", "",
        ISOGL_SYM_PARAM_TEXTS, 0, 0, 0
    },
    [CEN_SEGMENTS] = {
        "segments", "Segmenti", "Non usati dal center mark", "",
        ISOGL_SYM_PARAM_SEGMENTS, 0, 0, 0
    },
    [CEN_SCALE] = {
        "scale", "Scala foglio", "Sola lettura; rigenera il simbolo", "1:n",
        ISOGL_SYM_PARAM_SCALE, 1, 1, 0
    },
    [CEN_SIZE] = {
        "size_mm", "Dimensione", "Lunghezza braccio su carta", "mm",
        ISOGL_SYM_PARAM_NUMBER, 1, 1, 1
    },
    [CEN_PATTERN] = {
        "pattern", "Tipo linea ISO", "Pattern del center mark", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_type_values, ISOGL_SYM_LINE_TYPE_VALUE_COUNT
    },
    [CEN_WEIGHT] = {
        "weight", "Peso ISO", "Spessore del center mark", "",
        ISOGL_SYM_PARAM_ENUM, 1, 1, 1,
        isogl_symbol_line_weight_values, ISOGL_SYM_LINE_WEIGHT_VALUE_COUNT
    },
    [CEN_LAYER] = {
        "layer", "Layer", "Layer di stile", "",
        ISOGL_SYM_PARAM_INTEGER, 1, 1, 1
    },
    [CEN_COLOR] = {
        "color", "Colore", "Colore di stampa", "",
        ISOGL_SYM_PARAM_COLOR, 1, 1, 1
    }
};

double isogl_symbol_gen_text_width(void *user, const char *utf8,
                                   double height_mm) {
    const isogl_symbol_gen_context *ctx = user;
    if (!ctx || !ctx->font || !utf8 || height_mm <= 0.0) return 0.0;

    double font_height = (double)ctx->font->height;
    if (font_height <= 0.0) return 0.0;

    double scale = ctx->sheet_scale > 0.0 ? ctx->sheet_scale : 1.0;
    return isogl_font_text_advance(ctx->font, utf8) * height_mm /
           (font_height * scale);
}

static int sym_dimension_init(isogl_symbol_params *params) {
    if (!params || params->param_count != DIM_PARAM_COUNT) return -1;

    for (size_t i = 0; i < 3; ++i) {
        params->params[DIM_POINTS].value.points[i].x = 0.0;
        params->params[DIM_POINTS].value.points[i].y = 0.0;
    }
    for (size_t i = 0; i < 2; ++i) {
        sym_params_set_text_internal(params, DIM_TEXTS, i, "");
    }
    params->params[DIM_DISTANCE].value.number = 0.0;
    params->params[DIM_TEXT_OFFSET].value.number = 0.0;
    params->params[DIM_TEXT_HEIGHT].value.number = 3.5;
    params->params[DIM_PRECISION].value.integer = 1;
    params->params[DIM_PICK_TEXT].value.boolean = 1;
    params->params[DIM_MARK_POINTS].value.boolean = 0;
    params->params[DIM_ARROW_LEFT].value.integer = ISOGL_DIM_ARROW_OPEN;
    params->params[DIM_ARROW_RIGHT].value.integer = ISOGL_DIM_ARROW_OPEN;
    params->params[DIM_PATTERN].value.integer = 0;
    params->params[DIM_WEIGHT].value.integer = LINE_THICKNESS_0_35;
    params->params[DIM_LAYER].value.integer = 0;
    params->params[DIM_COLOR].value.color[0] = 1.0f;
    params->params[DIM_COLOR].value.color[1] = 1.0f;
    params->params[DIM_COLOR].value.color[2] = 1.0f;
    params->params[DIM_COLOR].value.color[3] = 1.0f;
    return 0;
}

static int sym_dimension_validate(const isogl_symbol_params *params) {
    if (!params || params->param_count != DIM_PARAM_COUNT) return -1;

    const isogl_symbol_param *points = &params->params[DIM_POINTS];
    const isogl_symbol_param *texts = &params->params[DIM_TEXTS];
    if (points->count != 3 || texts->count != 2) return -1;

    double height = params->params[DIM_TEXT_HEIGHT].value.number;
    if (!(height > 0.0) || !isfinite(height)) return -1;

    int precision = params->params[DIM_PRECISION].value.integer;
    if (precision < 0 || precision > 12) return -1;

    int left = params->params[DIM_ARROW_LEFT].value.integer;
    int right = params->params[DIM_ARROW_RIGHT].value.integer;
    if (left < ISOGL_DIM_ARROW_NONE ||
        left > ISOGL_DIM_ARROW_CLOSED_OUTSIDE ||
        right < ISOGL_DIM_ARROW_NONE ||
        right > ISOGL_DIM_ARROW_CLOSED_OUTSIDE) {
        return -1;
    }

    int pattern = params->params[DIM_PATTERN].value.integer;
    int weight = params->params[DIM_WEIGHT].value.integer;
    int layer = params->params[DIM_LAYER].value.integer;
    if (pattern < 0 || pattern >= LINE_TYPE_COUNT) return -1;
    if (weight < 0 || weight >= LINE_THICKNESS_COUNT) return -1;
    if (layer < 0 || layer > 31) return -1;
    return 0;
}

static int sym_dimension_generate(const isogl_symbol_params *params,
                                  const isogl_symbol_gen_context *ctx,
                                  IsoglSymEmitter *render,
                                  IsoglSymPickEmitter *pick) {
    if (!params || !ctx ||
        sym_dimension_validate(params) != 0) return -1;

    IsoglDimensionDesc d;
    memset(&d, 0, sizeof(d));
    const isogl_symbol_param *points = &params->params[DIM_POINTS];
    const isogl_symbol_param *texts = &params->params[DIM_TEXTS];

    d.x0 = points->value.points[0].x;
    d.y0 = points->value.points[0].y;
    d.x1 = points->value.points[1].x;
    d.y1 = points->value.points[1].y;
    d.px = points->value.points[2].x;
    d.py = points->value.points[2].y;

    snprintf(d.text_left, sizeof(d.text_left), "%s",
             texts->value.texts[0] ? texts->value.texts[0] : "");
    snprintf(d.text_right, sizeof(d.text_right), "%s",
             texts->value.texts[1] ? texts->value.texts[1] : "");

    d.distance = params->params[DIM_DISTANCE].value.number;
    d.text_offset_mm = params->params[DIM_TEXT_OFFSET].value.number;
    d.text_height_mm = params->params[DIM_TEXT_HEIGHT].value.number;
    d.sheet_scale = ctx->sheet_scale;
    d.precision = params->params[DIM_PRECISION].value.integer;
    d.pick_text = params->params[DIM_PICK_TEXT].value.boolean;
    d.mark_points = params->params[DIM_MARK_POINTS].value.boolean;
    d.arrow_left =
        (IsoglDimensionArrow)params->params[DIM_ARROW_LEFT].value.integer;
    d.arrow_right =
        (IsoglDimensionArrow)params->params[DIM_ARROW_RIGHT].value.integer;
    d.pattern = (uint8_t)params->params[DIM_PATTERN].value.integer;
    d.weight = (uint8_t)params->params[DIM_WEIGHT].value.integer;
    d.layer = (uint8_t)params->params[DIM_LAYER].value.integer;
    memcpy(d.color, params->params[DIM_COLOR].value.color, sizeof(d.color));

    IsoglSymMetrics metrics = {(void *)ctx, isogl_symbol_gen_text_width};
    int rc = 0;
    if (render && render->emit) {
        rc = isogl_dimension_geometry(&d, &metrics, render);
    }
    if (rc == 0 && pick && pick->emit) {
        rc = isogl_dimension_pick_geometry(&d, &metrics, pick);
    }
    return rc == 0 ? 0 : -1;
}

static int sym_center_init(isogl_symbol_params *params) {
    if (!params || params->param_count != CEN_PARAM_COUNT) return -1;
    params->params[CEN_POINTS].value.points[0].x = 0.0;
    params->params[CEN_POINTS].value.points[0].y = 0.0;
    params->params[CEN_SIZE].value.number = 8.0;
    params->params[CEN_PATTERN].value.integer = 0;
    params->params[CEN_WEIGHT].value.integer = LINE_THICKNESS_0_35;
    params->params[CEN_LAYER].value.integer = 0;
    params->params[CEN_COLOR].value.color[0] = 1.0f;
    params->params[CEN_COLOR].value.color[1] = 1.0f;
    params->params[CEN_COLOR].value.color[2] = 1.0f;
    params->params[CEN_COLOR].value.color[3] = 1.0f;
    return 0;
}

static int sym_center_validate(const isogl_symbol_params *params) {
    if (!params || params->param_count != CEN_PARAM_COUNT) return -1;
    if (params->params[CEN_POINTS].count != 1) return -1;
    double size = params->params[CEN_SIZE].value.number;
    if (!(size > 0.0) || !isfinite(size)) return -1;
    int pattern = params->params[CEN_PATTERN].value.integer;
    int weight = params->params[CEN_WEIGHT].value.integer;
    int layer = params->params[CEN_LAYER].value.integer;
    if (pattern < 0 || pattern >= LINE_TYPE_COUNT) return -1;
    if (weight < 0 || weight >= LINE_THICKNESS_COUNT) return -1;
    if (layer < 0 || layer > 31) return -1;
    return 0;
}

static int sym_center_generate(const isogl_symbol_params *params,
                               const isogl_symbol_gen_context *ctx,
                               IsoglSymEmitter *render,
                               IsoglSymPickEmitter *pick) {
    if (!params || !ctx || sym_center_validate(params) != 0) return -1;

    IsoglCenterMarkDesc d;
    memset(&d, 0, sizeof(d));
    d.cx = params->params[CEN_POINTS].value.points[0].x;
    d.cy = params->params[CEN_POINTS].value.points[0].y;
    d.size_mm = params->params[CEN_SIZE].value.number;
    d.sheet_scale = ctx->sheet_scale;
    d.pattern = (uint8_t)params->params[CEN_PATTERN].value.integer;
    d.weight = (uint8_t)params->params[CEN_WEIGHT].value.integer;
    d.layer = (uint8_t)params->params[CEN_LAYER].value.integer;
    memcpy(d.color, params->params[CEN_COLOR].value.color, sizeof(d.color));

    int rc = 0;
    if (render && render->emit) {
        rc = isogl_center_mark_geometry(&d, render);
    }
    if (rc == 0 && pick && pick->emit) {
        rc = isogl_center_mark_pick_geometry(&d, pick);
    }
    return rc == 0 ? 0 : -1;
}

static const isogl_symbol_type dimension_type = {
    .name = "dimension",
    .label = "Quota allineata",
    .description = "Quota con linea, riferimenti, frecce e testo",
    .params = DIM_PARAMS,
    .param_count = DIM_PARAM_COUNT,
    .init_params = sym_dimension_init,
    .validate = sym_dimension_validate,
    .generate = sym_dimension_generate
};

static const isogl_symbol_type center_mark_type = {
    .name = "center_mark",
    .label = "Center mark",
    .description = "Croce di centro",
    .params = CEN_PARAMS,
    .param_count = CEN_PARAM_COUNT,
    .init_params = sym_center_init,
    .validate = sym_center_validate,
    .generate = sym_center_generate
};

/* ---- type registry ------------------------------------------------------ */

int isogl_symbol_register_type(const isogl_symbol_type *type) {
    if (!type || !type->name || type->name[0] == '\0' ||
        !type->params || type->param_count == 0 || !type->generate) {
        return -1;
    }
    if (strcmp(type->name, "dimension") == 0 && type != &dimension_type) {
        return -1;
    }
    if (strcmp(type->name, "center_mark") == 0 && type != &center_mark_type) {
        return -1;
    }
    if (strcmp(type->name, "table_row") == 0 &&
        type != isogl_symbol_table_row_type()) {
        return -1;
    }
    if (strcmp(type->name, "dimension_diameter") == 0 &&
        type != isogl_symbol_diameter_type()) {
        return -1;
    }
    if (strcmp(type->name, "dimension_radius") == 0 &&
        type != isogl_symbol_radius_type()) {
        return -1;
    }

    for (size_t i = 0; i < symbol_type_count; ++i) {
        if (strcmp(symbol_types[i]->name, type->name) == 0) return -1;
    }

    if (symbol_type_count == symbol_type_capacity) {
        size_t capacity = symbol_type_capacity ? symbol_type_capacity * 2 : 8;
        const isogl_symbol_type **grown =
            realloc(symbol_types, capacity * sizeof(*grown));
        if (!grown) return -1;
        symbol_types = grown;
        symbol_type_capacity = capacity;
    }

    symbol_types[symbol_type_count++] = type;
    return 0;
}

const isogl_symbol_type *isogl_symbol_find_type(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < symbol_type_count; ++i) {
        if (strcmp(symbol_types[i]->name, name) == 0) return symbol_types[i];
    }
    return NULL;
}

size_t isogl_symbol_type_count(void) {
    return symbol_type_count;
}

const isogl_symbol_type *isogl_symbol_type_at(size_t index) {
    if (index >= symbol_type_count) return NULL;
    return symbol_types[index];
}

static int sym_register_builtins(void) {
    if (builtins_registered) return 0;

    const isogl_symbol_type *found = isogl_symbol_find_type("dimension");
    if (found && found != &dimension_type) return -1;
    if (!found && isogl_symbol_register_type(&dimension_type) != 0) return -1;

    found = isogl_symbol_find_type("center_mark");
    if (found && found != &center_mark_type) return -1;
    if (!found && isogl_symbol_register_type(&center_mark_type) != 0) return -1;

    const isogl_symbol_type *table_row = isogl_symbol_table_row_type();
    found = isogl_symbol_find_type("table_row");
    if (found && found != table_row) return -1;
    if (!found && isogl_symbol_register_type(table_row) != 0) return -1;

    const isogl_symbol_type *diameter = isogl_symbol_diameter_type();
    found = isogl_symbol_find_type("dimension_diameter");
    if (found && found != diameter) return -1;
    if (!found && isogl_symbol_register_type(diameter) != 0) return -1;

    const isogl_symbol_type *radius = isogl_symbol_radius_type();
    found = isogl_symbol_find_type("dimension_radius");
    if (found && found != radius) return -1;
    if (!found && isogl_symbol_register_type(radius) != 0) return -1;

    builtins_registered = 1;
    return 0;
}

/* ---- generation staging ------------------------------------------------- */

static int sym_build_push_text(SymBuild *b, const IsoglSymItem *item) {
    if (b->text_count == b->text_capacity) {
        size_t capacity = b->text_capacity ? b->text_capacity * 2 : 4;
        SymTextItem *grown = realloc(b->texts, capacity * sizeof(*grown));
        if (!grown) return -1;
        b->texts = grown;
        b->text_capacity = capacity;
    }

    SymTextItem *t = &b->texts[b->text_count];
    memset(t, 0, sizeof(*t));
    t->utf8 = sym_strdup(item->utf8 ? item->utf8 : "");
    if (!t->utf8) return -1;
    t->x = item->x;
    t->y = item->y;
    t->height_mm = item->height_mm;
    t->angle = item->angle;
    t->align = item->align;
    memcpy(t->color, item->color, sizeof(t->color));
    b->text_count++;
    return 0;
}

static int sym_build_push_pick(SymBuild *b, const IsoglSymPickItem *item) {
    if (b->pick_count == b->pick_capacity) {
        size_t capacity = b->pick_capacity ? b->pick_capacity * 2 : 16;
        IsoglSymPickItem *grown =
            realloc(b->picks, capacity * sizeof(*grown));
        if (!grown) return -1;
        b->picks = grown;
        b->pick_capacity = capacity;
    }
    b->picks[b->pick_count++] = *item;
    return 0;
}

static int sym_build_item(void *user, const IsoglSymItem *item) {
    SymBuild *b = user;
    if (!b || !item) return -1;

    if (item->kind == ISOGL_SYM_SEGMENT) {
        isogl_vertex a;
        isogl_vertex c;
        memset(&a, 0, sizeof(a));
        a.position[0] = (float)item->x0;
        a.position[1] = (float)item->y0;
        memcpy(a.color, item->color, sizeof(a.color));
        a.distanceFromStart = 0.0f;
        a.pk_thickness_pattern_layer =
            isogl_pack(item->weight, item->pattern, item->layer);

        c = a;
        c.position[0] = (float)item->x1;
        c.position[1] = (float)item->y1;
        c.distanceFromStart =
            (float)hypot(item->x1 - item->x0, item->y1 - item->y0);
        return isogl_segment_list_push_pair(&b->segments, a, c);
    }

    if (item->kind == ISOGL_SYM_TEXT) {
        return sym_build_push_text(b, item);
    }
    return 0;
}

static int sym_build_pick_item(void *user, const IsoglSymPickItem *item) {
    SymBuild *b = user;
    if (!b || !item) return -1;
    return sym_build_push_pick(b, item);
}

static void sym_build_free_contents(SymBuild *b) {
    if (!b) return;
    isogl_segment_list_free(&b->segments);
    for (size_t i = 0; i < b->text_count; ++i) free(b->texts[i].utf8);
    free(b->texts);
    free(b->picks);
    memset(b, 0, sizeof(*b));
}

static int sym_build(const isogl_symbol_type *type,
                     const isogl_symbol_params *params, double scale,
                     SymBuild *build) {
    if (!type || !params || !build) return -1;

    memset(build, 0, sizeof(*build));
    if (isogl_segment_list_init(&build->segments, 16) != 0) return -1;

    isogl_symbol_gen_context ctx = {
        .font = symbol_font,
        .sheet_scale = scale > 0.0 ? scale : 1.0
    };
    IsoglSymEmitter render = {build, sym_build_item};
    IsoglSymPickEmitter pick = {build, sym_build_pick_item};

    int rc = type->generate(params, &ctx, &render, &pick);
    if (rc != 0 ||
        (build->segments.size == 0 && build->text_count == 0)) {
        sym_build_free_contents(build);
        return -1;
    }
    return 0;
}

/* ---- GL resources ------------------------------------------------------- */

static void sym_remove_strings(isogl_symbol_id id) {
    while (isogl_string_list_remove_by_pick_id(&symbol_strings, id) == 0) {}
}

static int sym_append_build_strings(const SymBuild *build, isogl_symbol_id id,
                                    double scale, int selected) {
    for (size_t i = 0; i < build->text_count; ++i) {
        const SymTextItem *t = &build->texts[i];
        isogl_glyph_string *string =
            isogl_create_glyph_string(t->utf8 ? t->utf8 : "", symbol_font);
        if (!string) return -1;

        isogl_glyph_string_set_color(string, t->color);
        if (!isogl_string_list_append(&symbol_strings, string,
                                      (float)t->x, (float)t->y,
                                      (float)t->height_mm, (float)scale,
                                      (float)t->angle, symbol_font, id)) {
            isogl_free_glyph_string(string);
            return -1;
        }
        symbol_strings.glyph_strings[symbol_strings.glyph_string_count - 1]
            .selected = selected;
    }
    return 0;
}

static int sym_apply_vbo(IsoglSymbol *s, const SymBuild *build,
                         isogl_symbol_id id) {
    if (build->segments.size > 0) {
        if (s->vbo_id != 0) {
            if (isogl_vbo_array_update(&symbol_parts, s->vbo_id,
                                       &build->segments) != 0) {
                return -1;
            }
        } else {
            GLuint vbo = isogl_vbo_array_add(&symbol_parts, &build->segments,
                                             ISOGL_VBO_SEGMENT);
            if (vbo == 0) return -1;
            isogl_vbo_array_set_pick_id(&symbol_parts, vbo, id);
            s->vbo_id = vbo;
        }
        isogl_vbo_object *object =
            isogl_vbo_array_get(&symbol_parts, s->vbo_id);
        if (object) object->selected = s->selected;
    } else if (s->vbo_id != 0) {
        isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
        s->vbo_id = 0;
    }
    return 0;
}

/* ---- create/update ------------------------------------------------------ */

static isogl_symbol_id sym_create(const isogl_symbol_type *type,
                                  const isogl_symbol_params *params,
                                  double scale) {
    if (!symbol_initialized || !symbol_font || !type || !params) return 0;
    if (sym_params_validate(type, params) != 0) return 0;

    SymBuild build;
    if (sym_build(type, params, scale, &build) != 0) return 0;

    isogl_symbol_id id = 0;
    IsoglSymbol *s = sym_new_record(&id);
    if (!s) {
        sym_build_free_contents(&build);
        return 0;
    }

    s->type = type;
    s->sheet_scale = scale > 0.0 ? scale : 1.0;
    if (sym_params_copy(params, &s->params, s->sheet_scale) != 0) {
        sym_build_free_contents(&build);
        s->alive = 0;
        return 0;
    }

    if (sym_apply_vbo(s, &build, id) != 0 ||
        sym_append_build_strings(&build, id, s->sheet_scale, 0) != 0) {
        sym_remove_strings(id);
        if (s->vbo_id) isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
        sym_params_free_contents(&s->params);
        s->alive = 0;
        sym_build_free_contents(&build);
        return 0;
    }

    s->picks = build.picks;
    s->pick_count = build.pick_count;
    build.picks = NULL;
    build.pick_count = 0;
    sym_build_free_contents(&build);
    return id;
}

static int sym_update(IsoglSymbol *s, const isogl_symbol_params *params) {
    if (!s || !params || params->type != s->type) return -1;
    if (sym_params_validate(s->type, params) != 0) return -1;

    /* The module-wide scale is authoritative; any SCALE value carried by the
     * incoming params object is ignored. */
    double scale = symbol_sheet_scale;
    isogl_symbol_id id = (isogl_symbol_id)(s - symbols) + 1;

    SymBuild build;
    if (sym_build(s->type, params, scale, &build) != 0) return -1;

    /* Create every new glyph string before touching the old resources. */
    isogl_glyph_string **fresh = NULL;
    size_t fresh_count = build.text_count;
    if (fresh_count > 0) {
        fresh = calloc(fresh_count, sizeof(*fresh));
        if (!fresh) {
            sym_build_free_contents(&build);
            return -1;
        }
        for (size_t i = 0; i < fresh_count; ++i) {
            const SymTextItem *t = &build.texts[i];
            fresh[i] = isogl_create_glyph_string(
                t->utf8 ? t->utf8 : "", symbol_font);
            if (!fresh[i]) {
                for (size_t j = 0; j < i; ++j) isogl_free_glyph_string(fresh[j]);
                free(fresh);
                sym_build_free_contents(&build);
                return -1;
            }
            isogl_glyph_string_set_color(fresh[i], t->color);
        }
    }

    /* Prepare the new parameter copy before committing anything either. */
    isogl_symbol_params fresh_params;
    if (sym_params_copy(params, &fresh_params, scale) != 0) {
        for (size_t i = 0; i < fresh_count; ++i) {
            isogl_free_glyph_string(fresh[i]);
        }
        free(fresh);
        sym_build_free_contents(&build);
        return -1;
    }

    /* Create the replacement VBO first; the old one is removed only at commit.
     * isogl_vbo_array_add() appends a new array entry, so a failure here
     * leaves the old VBO untouched. */
    GLuint new_vbo = 0;
    if (build.segments.size > 0) {
        new_vbo = isogl_vbo_array_add(&symbol_parts, &build.segments,
                                      ISOGL_VBO_SEGMENT);
        if (new_vbo == 0) {
            for (size_t i = 0; i < fresh_count; ++i) {
                isogl_free_glyph_string(fresh[i]);
            }
            free(fresh);
            sym_params_free_contents(&fresh_params);
            sym_build_free_contents(&build);
            return -1;
        }
        isogl_vbo_array_set_pick_id(&symbol_parts, new_vbo, id);
        isogl_vbo_object *object = isogl_vbo_array_get(&symbol_parts, new_vbo);
        if (object) object->selected = s->selected;
    }

    /* Append the new strings with pick_id 0.  On failure only the newly
     * appended (still unpromoted) strings are removed; the old strings, which
     * carry the real id, are never touched. */
    for (size_t i = 0; i < fresh_count; ++i) {
        const SymTextItem *t = &build.texts[i];
        if (!isogl_string_list_append(&symbol_strings, fresh[i],
                                      (float)t->x, (float)t->y,
                                      (float)t->height_mm, (float)scale,
                                      (float)t->angle, symbol_font, 0)) {
            isogl_free_glyph_string(fresh[i]);
            for (size_t j = i + 1; j < fresh_count; ++j) {
                isogl_free_glyph_string(fresh[j]);
            }
            free(fresh);
            while (isogl_string_list_remove_by_pick_id(&symbol_strings, 0) == 0) {}
            if (new_vbo != 0) isogl_vbo_array_remove(&symbol_parts, new_vbo);
            sym_params_free_contents(&fresh_params);
            sym_build_free_contents(&build);
            return -1;
        }
    }
    free(fresh);

    /* Commit the string set: drop the old strings, then promote the new ones
     * (temporary pick_id 0) to the real pick id and restore selection. */
    sym_remove_strings(id);
    for (uint32_t i = 0; i < symbol_strings.glyph_string_count; ++i) {
        isogl_glyph_string *string = &symbol_strings.glyph_strings[i];
        if (string->pick_id == 0) {
            string->pick_id = id;
            string->selected = s->selected;
        }
    }

    /* Commit the VBO: remove the old buffer only now that everything worked. */
    if (s->vbo_id != 0 && s->vbo_id != new_vbo) {
        isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
    }
    s->vbo_id = new_vbo;

    free(s->picks);
    s->picks = build.picks;
    s->pick_count = build.pick_count;
    build.picks = NULL;
    build.pick_count = 0;

    sym_params_free_contents(&s->params);
    s->params = fresh_params;
    s->sheet_scale = scale;

    sym_build_free_contents(&build);
    return 0;
}

/* ---- public lifecycle --------------------------------------------------- */

int isogl_symbol_init(isogl_font_data *font) {
    if (symbol_initialized) {
        if (font) symbol_font = font;
        return 0;
    }
    if (!font) return -1;
    if (sym_register_builtins() != 0) return -1;
    if (isogl_vbo_array_init(&symbol_parts) != 0) return -1;

    memset(&symbol_strings, 0, sizeof(symbol_strings));
    symbols = NULL;
    symbol_slots = 0;
    symbol_capacity = 0;
    next_symbol_id = 1;
    symbol_font = font;
    symbol_initialized = 1;
    return 0;
}

void isogl_symbol_cleanup(void) {
    if (!symbol_initialized) return;

    for (size_t i = 0; i < symbol_slots; ++i) {
        IsoglSymbol *s = &symbols[i];
        if (!s->alive) continue;
        sym_params_free_contents(&s->params);
        free(s->picks);
        s->picks = NULL;
        s->pick_count = 0;
    }

    isogl_string_list_free(&symbol_strings);
    isogl_vbo_array_free(&symbol_parts);
    free(symbols);
    symbols = NULL;
    symbol_slots = 0;
    symbol_capacity = 0;
    next_symbol_id = 1;
    symbol_font = NULL;
    symbol_sheet_scale = 1.0;
    symbol_initialized = 0;
}

isogl_symbol_id isogl_symbol_create(const char *type_name,
                                    const isogl_symbol_params *params,
                                    double sheet_scale) {
    /* The module-wide scale is authoritative.  Keep the argument for ABI
     * compatibility, but do not let it override the global. */
    (void)sheet_scale;
    const isogl_symbol_type *type = isogl_symbol_find_type(type_name);
    if (!type || !params) return 0;
    return sym_create(type, params, symbol_sheet_scale);
}

double isogl_symbol_get_sheet_scale(void) {
    return symbol_sheet_scale;
}

int isogl_symbol_set_sheet_scale(double scale) {
    if (!isfinite(scale) || !(scale > 0.0)) return -1;

    symbol_sheet_scale = scale;
    if (!symbol_initialized) return 0;

    /* Regenerate every live symbol in place.  sym_update() is atomic, so a
     * failing symbol keeps its previous geometry and applied scale. */
    int failed = 0;
    for (size_t i = 0; i < symbol_slots; ++i) {
        IsoglSymbol *s = &symbols[i];
        if (!s->alive) continue;
        if (sym_update(s, &s->params) != 0) failed = 1;
    }
    return failed ? -1 : 0;
}

isogl_symbol_params *isogl_symbol_params_create(const char *type_name) {
    const isogl_symbol_type *type = isogl_symbol_find_type(type_name);
    if (!type) return NULL;

    isogl_symbol_params *params = malloc(sizeof(*params));
    if (!params) return NULL;
    if (sym_params_alloc(type, params, 1.0) != 0) {
        free(params);
        return NULL;
    }
    return params;
}

void isogl_symbol_params_free(isogl_symbol_params *params) {
    if (!params) return;
    sym_params_free_contents(params);
    free(params);
}

int isogl_symbol_params_get(isogl_symbol_id id, isogl_symbol_params **out) {
    if (!out) return -1;
    *out = NULL;
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;

    isogl_symbol_params *copy = malloc(sizeof(*copy));
    if (!copy) return -1;
    if (sym_params_copy(&s->params, copy, s->sheet_scale) != 0) {
        free(copy);
        return -1;
    }
    *out = copy;
    return 0;
}

int isogl_symbol_params_update(isogl_symbol_id id,
                               const isogl_symbol_params *params) {
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;
    return sym_update(s, params);
}

isogl_symbol_id isogl_symbol_create_dimension(const IsoglDimensionDesc *d) {
    if (!d) return 0;
    isogl_symbol_params params;
    if (sym_params_alloc(&dimension_type, &params,
                         symbol_sheet_scale) != 0) {
        return 0;
    }

    isogl_symbol_point *points = params.params[DIM_POINTS].value.points;
    points[0].x = d->x0; points[0].y = d->y0;
    points[1].x = d->x1; points[1].y = d->y1;
    points[2].x = d->px; points[2].y = d->py;
    sym_params_set_text_internal(&params, DIM_TEXTS, 0, d->text_left);
    sym_params_set_text_internal(&params, DIM_TEXTS, 1, d->text_right);
    params.params[DIM_DISTANCE].value.number = d->distance;
    params.params[DIM_TEXT_OFFSET].value.number = d->text_offset_mm;
    params.params[DIM_TEXT_HEIGHT].value.number = d->text_height_mm;
    params.params[DIM_PRECISION].value.integer = d->precision;
    params.params[DIM_PICK_TEXT].value.boolean = d->pick_text;
    params.params[DIM_MARK_POINTS].value.boolean = d->mark_points;
    params.params[DIM_ARROW_LEFT].value.integer = (int)d->arrow_left;
    params.params[DIM_ARROW_RIGHT].value.integer = (int)d->arrow_right;
    params.params[DIM_PATTERN].value.integer = d->pattern;
    params.params[DIM_WEIGHT].value.integer = d->weight;
    params.params[DIM_LAYER].value.integer = d->layer;
    memcpy(params.params[DIM_COLOR].value.color, d->color,
           sizeof(d->color));

    isogl_symbol_id id = isogl_symbol_create("dimension", &params,
                                             d->sheet_scale);
    sym_params_free_contents(&params);
    return id;
}

isogl_symbol_id isogl_symbol_create_center_mark(const IsoglCenterMarkDesc *d) {
    if (!d) return 0;
    isogl_symbol_params params;
    if (sym_params_alloc(&center_mark_type, &params,
                         symbol_sheet_scale) != 0) {
        return 0;
    }

    params.params[CEN_POINTS].value.points[0].x = d->cx;
    params.params[CEN_POINTS].value.points[0].y = d->cy;
    params.params[CEN_SIZE].value.number = d->size_mm;
    params.params[CEN_PATTERN].value.integer = d->pattern;
    params.params[CEN_WEIGHT].value.integer = d->weight;
    params.params[CEN_LAYER].value.integer = d->layer;
    memcpy(params.params[CEN_COLOR].value.color, d->color,
           sizeof(d->color));

    isogl_symbol_id id = isogl_symbol_create("center_mark", &params,
                                             d->sheet_scale);
    sym_params_free_contents(&params);
    return id;
}

int isogl_symbol_update_dimension(isogl_symbol_id id,
                                  const IsoglDimensionDesc *d) {
    if (!d) return -1;
    IsoglSymbol *s = sym_get(id);
    if (!s || s->type != &dimension_type) return -1;

    isogl_symbol_params params;
    if (sym_params_alloc(&dimension_type, &params,
                         symbol_sheet_scale) != 0) {
        return -1;
    }

    isogl_symbol_point *points = params.params[DIM_POINTS].value.points;
    points[0].x = d->x0; points[0].y = d->y0;
    points[1].x = d->x1; points[1].y = d->y1;
    points[2].x = d->px; points[2].y = d->py;
    sym_params_set_text_internal(&params, DIM_TEXTS, 0, d->text_left);
    sym_params_set_text_internal(&params, DIM_TEXTS, 1, d->text_right);
    params.params[DIM_DISTANCE].value.number = d->distance;
    params.params[DIM_TEXT_OFFSET].value.number = d->text_offset_mm;
    params.params[DIM_TEXT_HEIGHT].value.number = d->text_height_mm;
    params.params[DIM_PRECISION].value.integer = d->precision;
    params.params[DIM_PICK_TEXT].value.boolean = d->pick_text;
    params.params[DIM_MARK_POINTS].value.boolean = d->mark_points;
    params.params[DIM_ARROW_LEFT].value.integer = (int)d->arrow_left;
    params.params[DIM_ARROW_RIGHT].value.integer = (int)d->arrow_right;
    params.params[DIM_PATTERN].value.integer = d->pattern;
    params.params[DIM_WEIGHT].value.integer = d->weight;
    params.params[DIM_LAYER].value.integer = d->layer;
    memcpy(params.params[DIM_COLOR].value.color, d->color,
           sizeof(d->color));

    int rc = isogl_symbol_params_update(id, &params);
    sym_params_free_contents(&params);
    return rc;
}

int isogl_symbol_update_center_mark(isogl_symbol_id id,
                                    const IsoglCenterMarkDesc *d) {
    if (!d) return -1;
    IsoglSymbol *s = sym_get(id);
    if (!s || s->type != &center_mark_type) return -1;

    isogl_symbol_params params;
    if (sym_params_alloc(&center_mark_type, &params, symbol_sheet_scale) != 0) {
        return -1;
    }

    params.params[CEN_POINTS].value.points[0].x = d->cx;
    params.params[CEN_POINTS].value.points[0].y = d->cy;
    params.params[CEN_SIZE].value.number = d->size_mm;
    params.params[CEN_PATTERN].value.integer = d->pattern;
    params.params[CEN_WEIGHT].value.integer = d->weight;
    params.params[CEN_LAYER].value.integer = d->layer;
    memcpy(params.params[CEN_COLOR].value.color, d->color,
           sizeof(d->color));

    int rc = isogl_symbol_params_update(id, &params);
    sym_params_free_contents(&params);
    return rc;
}

int isogl_symbol_destroy(isogl_symbol_id id) {
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;

    if (s->vbo_id != 0) {
        isogl_vbo_array_remove(&symbol_parts, s->vbo_id);
        s->vbo_id = 0;
    }
    sym_remove_strings(id);
    sym_params_free_contents(&s->params);
    free(s->picks);
    s->picks = NULL;
    s->pick_count = 0;

    s->alive = 0;
    s->visible = 0;
    s->selected = 0;
    return 0;
}

void isogl_symbol_draw_all(void) {
    if (!symbol_initialized) return;

    isogl_draw_all_vbo_objects(&symbol_parts);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    isogl_draw_string_list(&symbol_strings, isogl_shader_program_font,
                           isogl_model_matrix, isogl_view_matrix, identity);
}

int isogl_symbol_pick_at(float px, float py, int extent_px,
                         uint32_t *ids, size_t max, size_t *count) {
    if (!symbol_initialized) {
        if (count) *count = 0;
        return -1;
    }
    return isogl_pick_at(&symbol_parts, &symbol_strings,
                         px, py, extent_px, ids, max, count);
}

int isogl_symbol_get_selected(isogl_symbol_id id) {
    IsoglSymbol *s = sym_get(id);
    return s ? s->selected : -1;
}

int isogl_symbol_set_selected(isogl_symbol_id id, int selected) {
    IsoglSymbol *s = sym_get(id);
    if (!s) return -1;

    s->selected = selected ? 1 : 0;

    if (s->vbo_id != 0) {
        isogl_vbo_object *object =
            isogl_vbo_array_get(&symbol_parts, s->vbo_id);
        if (object) object->selected = s->selected;
    }

    for (uint32_t i = 0; i < symbol_strings.glyph_string_count; ++i) {
        if (symbol_strings.glyph_strings[i].pick_id == id) {
            symbol_strings.glyph_strings[i].selected = s->selected;
        }
    }
    return 0;
}

/* ---- pick/bounds predicates (double, model units) ---------------------- */

static int sym_box_point(const isogl_box *b, double x, double y) {
    return x >= b->xmin && x <= b->xmax && y >= b->ymin && y <= b->ymax;
}

static int sym_segment_intersects_box(double ax, double ay,
                                      double bx, double by,
                                      const isogl_box *b) {
    if (sym_box_point(b, ax, ay) || sym_box_point(b, bx, by)) return 1;

    double dx = bx - ax;
    double dy = by - ay;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {ax - b->xmin, b->xmax - ax,
                   ay - b->ymin, b->ymax - ay};
    double t0 = 0.0;
    double t1 = 1.0;

    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return 0;
        } else {
            double r = q[i] / p[i];
            if (p[i] < 0.0) {
                if (r > t0) t0 = r;
            } else {
                if (r < t1) t1 = r;
            }
            if (t0 > t1) return 0;
        }
    }
    return 1;
}

static int sym_point_in_obb(double px, double py, const double c[4][2]) {
    int positive = 0;
    int negative = 0;
    for (int i = 0; i < 4; ++i) {
        double ax = c[i][0];
        double ay = c[i][1];
        double bx = c[(i + 1) & 3][0];
        double by = c[(i + 1) & 3][1];
        double cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        if (cross > 1e-9) positive = 1;
        else if (cross < -1e-9) negative = 1;
        if (positive && negative) return 0;
    }
    return 1;
}

static int sym_obb_contained(const double c[4][2], const isogl_box *b) {
    for (int i = 0; i < 4; ++i) {
        if (!sym_box_point(b, c[i][0], c[i][1])) return 0;
    }
    return 1;
}

static int sym_obb_intersects_box(const double c[4][2],
                                  const isogl_box *b) {
    for (int i = 0; i < 4; ++i) {
        int n = (i + 1) & 3;
        if (sym_segment_intersects_box(c[i][0], c[i][1],
                                       c[n][0], c[n][1], b)) {
            return 1;
        }
    }

    double rx[4] = {b->xmin, b->xmax, b->xmax, b->xmin};
    double ry[4] = {b->ymin, b->ymin, b->ymax, b->ymax};
    for (int i = 0; i < 4; ++i) {
        if (sym_point_in_obb(rx[i], ry[i], c)) return 1;
    }
    return 0;
}

typedef struct {
    isogl_box query;
    int crossing;
    int contained;
    int intersects;
    int has_bounds;
    double xmin, ymin, xmax, ymax;
} SymHitContext;

static void sym_bounds_point(SymHitContext *c, double x, double y) {
    if (!c->has_bounds) {
        c->xmin = c->xmax = x;
        c->ymin = c->ymax = y;
        c->has_bounds = 1;
        return;
    }
    if (x < c->xmin) c->xmin = x;
    if (x > c->xmax) c->xmax = x;
    if (y < c->ymin) c->ymin = y;
    if (y > c->ymax) c->ymax = y;
}

static int sym_apply_pick(const IsoglSymbol *s, SymHitContext *c) {
    if (!s || !c || !s->picks || s->pick_count == 0) return -1;

    for (size_t i = 0; i < s->pick_count; ++i) {
        const IsoglSymPickItem *item = &s->picks[i];
        if (item->kind == ISOGL_SYM_PICK_SEGMENT) {
            int contained = sym_box_point(&c->query, item->x0, item->y0) &&
                            sym_box_point(&c->query, item->x1, item->y1);
            int intersects = sym_segment_intersects_box(
                item->x0, item->y0, item->x1, item->y1, &c->query);
            if (c->crossing) {
                if (contained || intersects) c->intersects = 1;
            } else if (!contained) {
                c->contained = 0;
            }
            sym_bounds_point(c, item->x0, item->y0);
            sym_bounds_point(c, item->x1, item->y1);
        } else if (item->kind == ISOGL_SYM_PICK_OBB) {
            int contained = sym_obb_contained(item->corners, &c->query);
            int intersects = sym_obb_intersects_box(item->corners, &c->query);
            if (c->crossing) {
                if (contained || intersects) c->intersects = 1;
            } else if (!contained) {
                c->contained = 0;
            }
            for (int j = 0; j < 4; ++j) {
                sym_bounds_point(c, item->corners[j][0], item->corners[j][1]);
            }
        }
    }
    return 0;
}

static void sym_normalize_box(isogl_box *b) {
    if (b->xmin > b->xmax) {
        double t = b->xmin; b->xmin = b->xmax; b->xmax = t;
    }
    if (b->ymin > b->ymax) {
        double t = b->ymin; b->ymin = b->ymax; b->ymax = t;
    }
}

int isogl_symbol_hit(isogl_symbol_id id, const isogl_box *query, int crossing) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !query) return -1;

    SymHitContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.query = *query;
    sym_normalize_box(&ctx.query);
    ctx.crossing = crossing ? 1 : 0;
    ctx.contained = 1;

    if (sym_apply_pick(s, &ctx) != 0 || !ctx.has_bounds) return -1;
    return ctx.crossing ? ctx.intersects : ctx.contained;
}

int isogl_symbol_bounds(isogl_symbol_id id, isogl_box *out) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !out) return -1;

    SymHitContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.contained = 1;

    if (sym_apply_pick(s, &ctx) != 0 || !ctx.has_bounds) return -1;
    out->xmin = ctx.xmin;
    out->ymin = ctx.ymin;
    out->xmax = ctx.xmax;
    out->ymax = ctx.ymax;
    return 0;
}

static int sym_ignore_pick(void *user, const IsoglSymPickItem *item) {
    (void)user;
    (void)item;
    return 0;
}

int isogl_symbol_explode(isogl_symbol_id id, IsoglSymEmitter *out) {
    IsoglSymbol *s = sym_get(id);
    if (!s || !out || !out->emit) return -1;

    isogl_symbol_gen_context ctx = {
        .font = symbol_font,
        .sheet_scale = s->sheet_scale
    };
    IsoglSymPickEmitter pick = {NULL, sym_ignore_pick};
    int rc = s->type->generate(&s->params, &ctx, out, &pick);
    if (rc != 0) return -1;
    return isogl_symbol_destroy(id);
}
