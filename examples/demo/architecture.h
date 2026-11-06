#ifndef ISOGL_DEMO_ARCHITECTURE_H
#define ISOGL_DEMO_ARCHITECTURE_H

#include "isogl.h"
#include "isogl_font.h"

/* Builds the architecture drawing of isogl on an A3 landscape sheet.
 * Geometry goes into segments; every label goes into strings.  The drawing
 * only uses public isogl primitives (segment VBO + vector text). */
typedef struct {
    isogl_segment_list *segments;
    isogl_string_list *strings;
    isogl_font_data *font;
    float global_scale;
} ArchDrawing;

int arch_build(ArchDrawing *drawing);

#endif /* ISOGL_DEMO_ARCHITECTURE_H */
