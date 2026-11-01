#define _GNU_SOURCE

#include <gtk/gtk.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <isogl/isogl.h>
#include <isogl/isogl_symbol.h>

/*
 * Minimal GTK4 host.  GTK owns the window, the GDK GL context and
 * presentation; isogl only needs the current context and the framebuffer
 * viewport.  Wayland and X11 both work through GDK's EGL/GLX backend.
 */

typedef struct {
    GtkWidget *area;
    isogl_font_data *font;
    isogl_vbo_array parts;
    isogl_string_list strings;
    int ready;
    int panning;
    int fb_width, fb_height;
    float cursor_x, cursor_y;   /* viewport-local framebuffer pixels */
    float last_x, last_y;
} GtkHost;

/*
 * GDK4 does not expose a GL proc-address loader.  EGL is the primary path
 * (Wayland and EGL-based X11); GLX is the fallback for X11; dlsym covers
 * core symbols exported by libGL.
 */
static void *gtk_gl_loader(const char *name) {
    void *p = (void *)eglGetProcAddress(name);
    if (p) return p;

    static void *(*glx_get_proc)(const unsigned char *) = NULL;
    if (!glx_get_proc) {
        void *libgl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (libgl) {
            glx_get_proc = (void *(*)(const unsigned char *))
                dlsym(libgl, "glXGetProcAddressARB");
        }
    }
    if (glx_get_proc) {
        p = glx_get_proc((const unsigned char *)name);
        if (p) return p;
    }
    return dlsym(RTLD_DEFAULT, name);
}

static void host_geometry(GtkHost *h) {
    isogl_segment_list list;
    if (isogl_segment_list_init(&list, 32) != 0) return;

    isogl_vertex v;
    const float margin = 10.0f;
    const float w = 420.0f, hgt = 594.0f;

    /* Sheet margin rectangle. */
    const float xs[4] = {margin, w - margin, w - margin, margin};
    const float ys[4] = {margin, margin, hgt - margin, hgt - margin};
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        memset(&v, 0, sizeof(v));
        v.color[0] = v.color[1] = v.color[2] = 0.9f;
        v.color[3] = 1.0f;
        v.pk_thickness_pattern_layer =
            isogl_pack(LINE_THICKNESS_0_7, LINE_TYPE_CONTINUOUS, 0);
        v.position[0] = xs[i];
        v.position[1] = ys[i];
        isogl_vertex v2 = v;
        v2.position[0] = xs[j];
        v2.position[1] = ys[j];
        isogl_segment_list_push_pair(&list, v, v2);
    }

    /* Diagonal cross. */
    memset(&v, 0, sizeof(v));
    v.color[0] = v.color[1] = v.color[2] = 0.7f;
    v.color[3] = 1.0f;
    v.pk_thickness_pattern_layer =
        isogl_pack(LINE_THICKNESS_0_35, LINE_TYPE_DASHED, 0);
    isogl_vertex b = v;
    v.position[0] = margin + 20.0f;
    v.position[1] = margin + 20.0f;
    b.position[0] = w - margin - 20.0f;
    b.position[1] = hgt - margin - 20.0f;
    isogl_segment_list_push_pair(&list, v, b);

    isogl_vbo_array_init(&h->parts);
    isogl_vbo_array_add(&h->parts, &list, ISOGL_VBO_SEGMENT);
    isogl_segment_list_free(&list);

    isogl_string_list strings = {0};
    isogl_glyph_string *text = isogl_create_glyph_string("isogl + GTK4", h->font);
    if (text) {
        isogl_string_list_append(&strings, text, 30.0f, 560.0f, 10.0f,
                                 0.5f, 0.0f, h->font, 0);
        h->strings = strings;
    }
}

static void on_realize(GtkGLArea *area, gpointer user_data) {
    GtkHost *h = user_data;
    gtk_gl_area_make_current(area);

    if (gtk_gl_area_get_error(area) != NULL) {
        g_warning("GtkGLArea realize failed");
        return;
    }

    if (gtk_gl_area_get_api(area) != GDK_GL_API_GL) {
        g_warning("isogl needs desktop OpenGL 3.3 core, not GLES");
        gtk_gl_area_set_error(area,
            g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                "desktop OpenGL required"));
        return;
    }

    if (!gladLoadGLLoader((GLADloadproc)gtk_gl_loader)) {
        g_warning("gladLoadGLLoader failed in GTK4 host");
        return;
    }

    if (isogl_init_with_asset_dir(NULL) != 0) {
        g_warning("isogl_init failed (set ISOGL_ASSET_DIR)");
        return;
    }

    char font_path[4096];
    if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                         font_path, sizeof(font_path)) != 0) {
        g_warning("font asset path failed");
        return;
    }
    h->font = isogl_font_load_from_toml(font_path);
    if (!h->font) {
        g_warning("font load failed");
        return;
    }
    isogl_font_init_vao();
    isogl_symbol_init(h->font);

    isogl_set_sheet(210.0f, 297.0f, (isogl_scale){1, 2});
    isogl_zoom_all_sheet();
    host_geometry(h);

    h->ready = 1;
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context,
                          gpointer user_data) {
    (void)context;
    GtkHost *h = user_data;

    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] > 0 && vp[3] > 0) {
        h->fb_width = vp[2];
        h->fb_height = vp[3];
    } else {
        int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
        h->fb_width = gtk_widget_get_width(GTK_WIDGET(area)) * scale;
        h->fb_height = gtk_widget_get_height(GTK_WIDGET(area)) * scale;
    }

    if (!h->ready || h->fb_width <= 0 || h->fb_height <= 0) return TRUE;

    isogl_viewport vp_isogl = {0, 0, h->fb_width, h->fb_height};
    isogl_set_viewport(&vp_isogl);
    isogl_begin_frame();

    isogl_clear(0.08f, 0.09f, 0.10f, 1.0f);
    isogl_draw_sheet();
    isogl_draw_all_vbo_objects(&h->parts);
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    isogl_draw_string_list(&h->strings, isogl_shader_program_font,
                           isogl_model_matrix, isogl_view_matrix, identity);
    isogl_symbol_draw_all();
    /* No swap: GtkGLArea presents after the render signal. */
    return TRUE;
}

static void on_unrealize(GtkGLArea *area, gpointer user_data) {
    GtkHost *h = user_data;
    if (!h->ready) return;
    gtk_gl_area_make_current(area);

    isogl_string_list_free(&h->strings);
    isogl_vbo_array_free(&h->parts);
    isogl_symbol_cleanup();
    isogl_font_free_data(h->font);
    isogl_cleanup();
    h->ready = 0;
}

static void local_to_fb(const GtkHost *h, double x, double y,
                        float *px, float *py) {
    int lw = gtk_widget_get_width(GTK_WIDGET(h->area));
    int lh = gtk_widget_get_height(GTK_WIDGET(h->area));
    if (lw > 0 && h->fb_width > 0) *px = (float)(x * h->fb_width / lw);
    else *px = (float)x;
    if (lh > 0 && h->fb_height > 0) *py = (float)(y * h->fb_height / lh);
    else *py = (float)y;
}

static int pick_extent(const GtkHost *h) {
    int lw = gtk_widget_get_width(GTK_WIDGET(h->area));
    double scale = (lw > 0 && h->fb_width > 0)
                       ? (double)h->fb_width / (double)lw
                       : 1.0;
    int extent = (int)lround(ISOGL_PICK_DEFAULT_EXTENT * scale);
    return extent > 0 ? extent : ISOGL_PICK_DEFAULT_EXTENT;
}

static void pick_at(GtkHost *h, float px, float py) {
    uint32_t ids[4096];
    size_t count = 0;
    int extent = pick_extent(h);

    if (isogl_pick_at(&h->parts, &h->strings, px, py, extent,
                      ids, sizeof(ids) / sizeof(ids[0]), &count) == 0) {
        g_print("pick at (%.1f,%.1f): %zu id(s)\n", px, py, count);
        for (size_t i = 0; i < count; ++i) g_print(" %u", ids[i]);
        if (count) g_print("\n");
    }

    uint32_t symbol_ids[64];
    size_t symbol_count = 0;
    if (isogl_symbol_pick_at(px, py, extent,
                             symbol_ids, 64, &symbol_count) == 0 &&
        symbol_count > 0) {
        g_print("symbol pick at (%.1f,%.1f): %zu id(s)\n",
                px, py, symbol_count);
    }
}

static void on_motion(GtkEventControllerMotion *controller,
                      double x, double y, gpointer user_data) {
    (void)controller;
    GtkHost *h = user_data;
    float px = 0.0f, py = 0.0f;
    local_to_fb(h, x, y, &px, &py);

    if (h->panning) {
        isogl_handle_pan(px - h->last_x, py - h->last_y);
        gtk_gl_area_queue_render(GTK_GL_AREA(h->area));
    }
    h->cursor_x = px;
    h->cursor_y = py;
    h->last_x = px;
    h->last_y = py;
}

static void on_pressed(GtkGestureClick *gesture, int n_press,
                       double x, double y, gpointer user_data) {
    (void)n_press;
    GtkHost *h = user_data;
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));

    float px = 0.0f, py = 0.0f;
    local_to_fb(h, x, y, &px, &py);

    if (button == GDK_BUTTON_SECONDARY || button == GDK_BUTTON_PRIMARY) {
        pick_at(h, px, py);
    } else if (button == GDK_BUTTON_MIDDLE) {
        h->panning = 1;
        h->last_x = px;
        h->last_y = py;
    }
}

static void on_released(GtkGestureClick *gesture, int n_press,
                        double x, double y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    GtkHost *h = user_data;
    h->panning = 0;
}

static gboolean on_scroll(GtkEventControllerScroll *controller,
                          double dx, double dy, gpointer user_data) {
    (void)controller;
    (void)dx;
    GtkHost *h = user_data;
    int wheel = dy < 0.0 ? 1 : -1;
    isogl_handle_zoom(wheel, h->cursor_x, h->cursor_y);
    gtk_gl_area_queue_render(GTK_GL_AREA(h->area));
    return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "isogl GTK4 host");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 720);

    GtkWidget *area = gtk_gl_area_new();
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 3);
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GL);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(area), TRUE);

    GtkHost *h = g_new0(GtkHost, 1);
    h->area = area;
    g_object_set_data_full(G_OBJECT(area), "isogl-host", h, g_free);

    g_signal_connect(area, "realize", G_CALLBACK(on_realize), h);
    g_signal_connect(area, "unrealize", G_CALLBACK(on_unrealize), h);
    g_signal_connect(area, "render", G_CALLBACK(on_render), h);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), h);
    gtk_widget_add_controller(area, motion);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), h);
    g_signal_connect(click, "released", G_CALLBACK(on_released), h);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), h);
    gtk_widget_add_controller(area, scroll);

    gtk_window_set_child(GTK_WINDOW(window), area);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.isogl.gtk4host",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
