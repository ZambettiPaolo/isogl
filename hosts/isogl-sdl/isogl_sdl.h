#ifndef ISOGL_SDL_H
#define ISOGL_SDL_H

#include <SDL2/SDL.h>
#include <isogl/isogl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional SDL2 host helper for isogl.
 *
 * This module is NOT part of libisogl: it owns the SDL window, the GL context
 * and the GL loader, and it is the only place in the project that knows both
 * SDL and isogl.  It centralises HiDPI conversion: isogl expects viewport-local
 * framebuffer pixels, while SDL mouse events are in logical window points.
 */

typedef struct {
    SDL_Window *window;
    SDL_GLContext context;
    int fb_width, fb_height;      /* drawable size, framebuffer pixels */
    int win_width, win_height;    /* logical window size, points       */
    double scale_x, scale_y;      /* fb / logical per axis             */
} isogl_sdl_host;

typedef struct {
    const char *title;
    int width, height;            /* logical window points             */
    int gl_major, gl_minor;       /* 0 -> 3.3 core                     */
    int multisample;              /* requested samples, 0 = off        */
    int vsync;                    /* 1 -> swap interval 1              */
    int hidden;                   /* 1 -> SDL_WINDOW_HIDDEN            */
} isogl_sdl_host_desc;

/* Create the window, the 3.3 core context and load glad.  Returns 0 on
 * success.  On success the context is current. */
int isogl_sdl_host_init(isogl_sdl_host *h, const isogl_sdl_host_desc *d);

/* Destroy the context and the window.  Safe on a zeroed struct. */
void isogl_sdl_host_shutdown(isogl_sdl_host *h);

/* Refresh win/fb sizes and scale factors.  Call after a resize. */
int isogl_sdl_host_update(isogl_sdl_host *h);

/* update() + isogl_set_viewport({0,0,fb_w,fb_h}) + isogl_begin_frame().
 * Call once per frame before clearing/drawing.  Returns 0 on success. */
int isogl_sdl_host_begin_frame(isogl_sdl_host *h);

/* Convert logical window coordinates to viewport-local framebuffer pixels. */
void isogl_sdl_host_local_to_pixels(const isogl_sdl_host *h,
                                    float lx, float ly, float *px, float *py);

/* Convert logical window deltas to framebuffer-pixel deltas. */
void isogl_sdl_host_delta_to_pixels(const isogl_sdl_host *h,
                                    float dx, float dy, float *pdx, float *pdy);

void isogl_sdl_host_swap(isogl_sdl_host *h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ISOGL_SDL_H */
