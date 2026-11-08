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
