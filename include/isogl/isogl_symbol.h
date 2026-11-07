#ifndef ISOGL_SYMBOL_H
#define ISOGL_SYMBOL_H

#include "isogl.h"
#include "isogl_symbol_geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * isogl-owned symbols (dimensions, center marks, ...).  The CAD document keeps
 * only an opaque isogl_symbol_id; it never reads the symbol's VBO or internal
 * segments.  It can ask for bounds/hit/pick and, to take ownership of the
 * geometry, explode the symbol (which destroys it).
 *
 * The module owns its VBO/string arrays and requires a current GL context for
 * create/draw/pick/destroy.  isogl_symbol_cleanup() must run before
 * isogl_cleanup().
 */

typedef uint32_t isogl_symbol_id;

/* ---- Generic, C-defined symbol types ------------------------------------
 *
 * A symbol type is code: it declares a parameter schema (with UI metadata),
 * validates values and generates render/pick items.  The file format stores
 * type name + parameter values only; it never defines generation logic.
 * The type descriptor and its strings must outlive every symbol using it.
 */

typedef enum {
    ISOGL_SYM_PARAM_POINTS = 0,  /* list of isogl_symbol_point */
    ISOGL_SYM_PARAM_TEXTS,       /* list of UTF-8 strings           */
    ISOGL_SYM_PARAM_SEGMENTS,    /* list of isogl_symbol_segment    */
    ISOGL_SYM_PARAM_SCALE,       /* read-only sheet scale (number)  */
    ISOGL_SYM_PARAM_NUMBER,
    ISOGL_SYM_PARAM_INTEGER,
    ISOGL_SYM_PARAM_BOOLEAN,
    ISOGL_SYM_PARAM_ENUM,
    ISOGL_SYM_PARAM_COLOR        /* float[4]                        */
} isogl_symbol_param_kind;

typedef struct {
    const char *key;             /* stable key for files/UI */
    const char *label;           /* human-readable name */
    const char *help;            /* optional description; may be NULL */
    const char *unit;            /* "mm", "deg", ""; may be NULL */
    isogl_symbol_param_kind kind;
    size_t min_count;            /* lists: minimum elements */
    size_t max_count;            /* lists: maximum; 0 = unbounded. Scalars: 1 */
    int editable;                /* 0 for scale and derived values */
} isogl_symbol_param_desc;

typedef struct {
    double x, y;
} isogl_symbol_point;

typedef struct {
    double x0, y0, x1, y1;
} isogl_symbol_segment;

typedef struct {
    const isogl_symbol_param_desc *desc;  /* points into the type schema */
    size_t count;                         /* 1 for scalars */
    union {
        isogl_symbol_point *points;
        char **texts;                     /* owned UTF-8 strings */
        isogl_symbol_segment *segments;
        double number;                    /* NUMBER and SCALE */
        int integer;
        int boolean;
        float color[4];
    } value;
} isogl_symbol_param;

struct isogl_symbol_type;

typedef struct {
    const struct isogl_symbol_type *type; /* not owned */
    isogl_symbol_param *params;           /* owned array, param_count entries */
    size_t param_count;
    double sheet_scale;                   /* read-only context copy */
} isogl_symbol_params;

typedef struct {
    const isogl_font_data *font;          /* not owned; may be NULL */
    double sheet_scale;
} isogl_symbol_gen_context;

typedef struct isogl_symbol_type {
    const char *name;                     /* stable, e.g. "dimension" */
    const char *label;                    /* UI name */
    const char *description;              /* UI description; may be NULL */
    const isogl_symbol_param_desc *params;
    size_t param_count;

    /* Optional: fill defaults after isogl_symbol_params_create(). */
    int (*init_params)(isogl_symbol_params *params);
    /* Validate all parameter values; return 0/-1. */
    int (*validate)(const isogl_symbol_params *params);
    /* Pure generation: no GL. Emit render and pick items. */
    int (*generate)(const isogl_symbol_params *params,
                    const isogl_symbol_gen_context *ctx,
                    IsoglSymEmitter *render,
                    IsoglSymPickEmitter *pick);
} isogl_symbol_type;

int isogl_symbol_register_type(const isogl_symbol_type *type);
const isogl_symbol_type *isogl_symbol_find_type(const char *name);
size_t isogl_symbol_type_count(void);
const isogl_symbol_type *isogl_symbol_type_at(size_t index);

/*
 * Module-wide sheet scale.  It is read-only from the parameter model: every
 * live symbol uses this value, and params_get() reports it through the SCALE
 * parameter (editable = 0).  params_update() ignores any SCALE value carried
 * by the incoming params object.
 *
 * isogl_symbol_set_sheet_scale() regenerates every live symbol in place,
 * preserving ID, pick ID and selection.  A symbol that fails to regenerate
 * keeps its previous geometry and applied scale; the setter continues with
 * the others and returns -1 if any failed, 0 otherwise.
 */
int isogl_symbol_set_sheet_scale(double scale);  /* finite and > 0 only */
double isogl_symbol_get_sheet_scale(void);       /* defaults to 1.0 */

/* Create an instance of a registered C-defined type.  sheet_scale is ignored:
 * the module-wide scale from isogl_symbol_set_sheet_scale() is authoritative
 * and is what the generator receives.  The argument stays for ABI
 * compatibility. */
isogl_symbol_id isogl_symbol_create(const char *type_name,
                                    const isogl_symbol_params *params,
                                    double sheet_scale);

/* Allocate params matching the type schema.  Lists start at min_count,
 * scalars at zero; init_params() fills type defaults. */
isogl_symbol_params *isogl_symbol_params_create(const char *type_name);
void isogl_symbol_params_free(isogl_symbol_params *params);

/* Deep copy the current parameters of a live symbol. */
int isogl_symbol_params_get(isogl_symbol_id id, isogl_symbol_params **out);

/* Replace all parameters of a live symbol, regenerating it in place.  ID,
 * pick ID and the selection flag are preserved.  The module-wide sheet scale
 * is applied; a SCALE value in `params` is ignored.  Atomic per symbol: on
 * validation/generation/allocation failure the old VBO, strings, pick items,
 * params and geometry stay intact. */
int isogl_symbol_params_update(isogl_symbol_id id,
                               const isogl_symbol_params *params);

/* Replace one text element, copying utf8 (NULL becomes ""). */
int isogl_symbol_params_set_text(isogl_symbol_params *params,
                                 size_t param_index, size_t element,
                                 const char *utf8);

/* font is used for dimension text layout; it must stay alive until cleanup. */
int isogl_symbol_init(isogl_font_data *font);
void isogl_symbol_cleanup(void);

isogl_symbol_id isogl_symbol_create_dimension(const IsoglDimensionDesc *d);
isogl_symbol_id isogl_symbol_create_center_mark(const IsoglCenterMarkDesc *d);

/*
 * Regenerate one symbol in place.  The symbol ID, pick ID and selection flag
 * are preserved; only its geometry and text are updated.  Returns 0 on
 * success, -1 on invalid/dead ID or generation failure (the symbol stays
 * alive).
 */
int isogl_symbol_update_dimension(isogl_symbol_id id,
                                  const IsoglDimensionDesc *d);
int isogl_symbol_update_center_mark(isogl_symbol_id id,
                                    const IsoglCenterMarkDesc *d);

int isogl_symbol_destroy(isogl_symbol_id id);

void isogl_symbol_draw_all(void);

/* Returns the distinct symbol IDs visible in the cursor box, centre-out.
 * px, py are viewport-local framebuffer pixels (origin top-left); extent_px
 * is the pick box side in framebuffer pixels. */
int isogl_symbol_pick_at(float px, float py, int extent_px,
                         uint32_t *ids, size_t max, size_t *count);

/*
 * Analytic rectangle predicate over the symbol's pick geometry.
 * crossing = 0: the symbol is entirely contained.
 * crossing != 0: the symbol intersects the query or is contained.
 * Returns 1 if selected, 0 if not, -1 on invalid arguments/dead ID.
 */
int isogl_symbol_hit(isogl_symbol_id id, const isogl_box *query, int crossing);

/* AABB of the symbol's pick geometry; 0 on success, -1 on error. */
int isogl_symbol_bounds(isogl_symbol_id id, isogl_box *out);

/* Set the persistent selection flag on the symbol's VBO and text string. */
int isogl_symbol_set_selected(isogl_symbol_id id, int selected);

/* Return 1/0 for a live symbol, -1 for an invalid/dead ID. */
int isogl_symbol_get_selected(isogl_symbol_id id);

/*
 * Emit the symbol's render geometry (segments and text runs) and, on success,
 * destroy the symbol.  On emitter failure the symbol is left alive.  The
 * emitted geometry is in model/document units; text height is paper mm.
 */
int isogl_symbol_explode(isogl_symbol_id id, IsoglSymEmitter *out);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_SYMBOL_H */
