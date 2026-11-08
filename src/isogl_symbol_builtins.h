#ifndef ISOGL_SYMBOL_BUILTINS_H
#define ISOGL_SYMBOL_BUILTINS_H

#include "isogl_symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal accessors for the symbol types implemented in dedicated C files
 * (one file per symbol).  isogl_symbol.c registers them from
 * sym_register_builtins(); the registry itself stays generic.
 *
 * The pure generators below take typed parameters plus a caller-supplied
 * metric callback.  They perform no GL/SDL calls and never touch the CAD
 * document, so headless tests can exercise them with a deterministic stub.
 * sheet_scale comes from ctx (0 or negative is treated as 1).
 */

const isogl_symbol_type *isogl_symbol_table_row_type(void);
const isogl_symbol_type *isogl_symbol_diameter_type(void);
const isogl_symbol_type *isogl_symbol_radius_type(void);

/*
 * Shared font metric callback for symbol generators: returns the text width in
 * model units from ctx->font at the requested paper height.  Dedicated type
 * files build an IsoglSymMetrics around it instead of duplicating the formula.
 */
double isogl_symbol_gen_text_width(void *user, const char *utf8,
                                   double height_mm);

int isogl_table_row_geometry(const isogl_symbol_params *params,
                             const isogl_symbol_gen_context *ctx,
                             const IsoglSymMetrics *metrics,
                             IsoglSymEmitter *out);

int isogl_table_row_pick_geometry(const isogl_symbol_params *params,
                                  const isogl_symbol_gen_context *ctx,
                                  const IsoglSymMetrics *metrics,
                                  IsoglSymPickEmitter *out);

#ifdef __cplusplus
}
#endif

#endif /* ISOGL_SYMBOL_BUILTINS_H */
