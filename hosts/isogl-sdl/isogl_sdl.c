#include "isogl_sdl.h"
#include "glad.h"

#include <string.h>

int isogl_sdl_host_init(isogl_sdl_host *h, const isogl_sdl_host_desc *d) {
    if (!h || !d) return -1;
    memset(h, 0, sizeof(*h));

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return -1;

    int major = d->gl_major > 0 ? d->gl_major : 3;
    int minor = d->gl_minor > 0 ? d->gl_minor : 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (d->multisample > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, d->multisample);
    }

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                   SDL_WINDOW_ALLOW_HIGHDPI;
    if (d->hidden) flags |= SDL_WINDOW_HIDDEN;

    int width = d->width > 0 ? d->width : 960;
    int height = d->height > 0 ? d->height : 720;
    h->window = SDL_CreateWindow(d->title ? d->title : "isogl",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 width, height, flags);
    if (!h->window) goto fail;

    h->context = SDL_GL_CreateContext(h->window);
    if (!h->context) goto fail;

    if (SDL_GL_MakeCurrent(h->window, h->context) != 0) goto fail;

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) goto fail;

    if (d->vsync) SDL_GL_SetSwapInterval(1);

    if (isogl_sdl_host_update(h) != 0) goto fail;
    return 0;

fail:
    isogl_sdl_host_shutdown(h);
    return -1;
}

void isogl_sdl_host_shutdown(isogl_sdl_host *h) {
    if (!h) return;
    if (h->context) SDL_GL_DeleteContext(h->context);
    if (h->window) SDL_DestroyWindow(h->window);
    memset(h, 0, sizeof(*h));
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int isogl_sdl_host_update(isogl_sdl_host *h) {
    if (!h || !h->window) return -1;

    SDL_GetWindowSize(h->window, &h->win_width, &h->win_height);
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(h->window, &h->fb_width, &h->fb_height);
#else
    SDL_GL_GetDrawableSize(h->window, &h->fb_width, &h->fb_height);
#endif

    if (h->win_width <= 0 || h->win_height <= 0 ||
        h->fb_width <= 0 || h->fb_height <= 0) {
        return -1;
    }

    h->scale_x = (double)h->fb_width / (double)h->win_width;
    h->scale_y = (double)h->fb_height / (double)h->win_height;
    return 0;
}

int isogl_sdl_host_begin_frame(isogl_sdl_host *h) {
    if (isogl_sdl_host_update(h) != 0) return -1;

    isogl_viewport vp = {0, 0, h->fb_width, h->fb_height};
    isogl_set_viewport(&vp);
    return isogl_begin_frame();
}

void isogl_sdl_host_local_to_pixels(const isogl_sdl_host *h,
                                    float lx, float ly,
                                    float *px, float *py) {
    if (!h) return;
    if (px) *px = (float)((double)lx * h->scale_x);
    if (py) *py = (float)((double)ly * h->scale_y);
}

void isogl_sdl_host_delta_to_pixels(const isogl_sdl_host *h,
                                    float dx, float dy,
                                    float *pdx, float *pdy) {
    if (!h) return;
    if (pdx) *pdx = (float)((double)dx * h->scale_x);
    if (pdy) *pdy = (float)((double)dy * h->scale_y);
}

void isogl_sdl_host_swap(isogl_sdl_host *h) {
    if (h && h->window) SDL_GL_SwapWindow(h->window);
}
