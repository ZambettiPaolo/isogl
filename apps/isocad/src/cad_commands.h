#ifndef ISOCAD_CAD_COMMANDS_H
#define ISOCAD_CAD_COMMANDS_H

#include <stddef.h>

#include "cad.h"
#include "console.h"
#include "console_cmd.h"

/*
 * Console command execution against the CAD-side symbol records
 * (CadSymbolRef: ID + type + parameter copy).  Parameter interpretation is
 * driven by the public isogl_symbol_param_desc metadata; no symbol internals
 * are read.  Diagnostics go to stdout, one-line summaries to the console.
 */

/* Execute a parsed command.  Returns 0 on success, -1 on failure.  A failure
 * is reported both on stdout and as a console line. */
int isocad_command_execute(CadDocument *doc, IsocadConsole *console,
                           const IsocadCommand *cmd);

/* One-line type/parameter summary, e.g. "S3 dimension 16p pts=3".  Safe with
 * a NULL reference (writes "no symbol"). */
void isocad_symbol_summary(const CadSymbolRef *ref, char *out, size_t size);

#endif /* ISOCAD_CAD_COMMANDS_H */
