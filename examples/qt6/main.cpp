#include <isogl/isogl.h>
#include <isogl/isogl_symbol.h>

#include <QApplication>
#include <QDebug>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <cmath>
#include <cstring>

/*
 * Minimal Qt6 host.  QOpenGLWidget owns the FBO and presentation; isogl only
 * needs the current context and the framebuffer viewport.
 *
 * Include order matters: isogl.h pulls in glad, which defines __gl_h_, so
 * Qt's later <GL/gl.h> inclusion is skipped.
 */

static void *qt_gl_loader(const char *name) {
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(name));
}

static void add_line(isogl_vbo_array *parts, isogl_segment_list *list,
                     float x0, float y0, float x1, float y1,
                     uint8_t weight, uint8_t pattern) {
    (void)parts;
    isogl_vertex a, b;
    std::memset(&a, 0, sizeof(a));
    std::memset(&b, 0, sizeof(b));
    a.color[0] = a.color[1] = a.color[2] = 0.9f;
    a.color[3] = 1.0f;
    b = a;
    a.position[0] = x0;
    a.position[1] = y0;
    b.position[0] = x1;
    b.position[1] = y1;
    uint16_t tpp = isogl_pack(weight, pattern, 0);
    a.pk_thickness_pattern_layer = tpp;
    b.pk_thickness_pattern_layer = tpp;
    b.distanceFromStart = 1.0f;
    isogl_segment_list_push_pair(list, a, b);
}

class CadView : public QOpenGLWidget {
public:
    explicit CadView(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    ~CadView() override {
        makeCurrent();
        if (ready_) {
            isogl_string_list_free(&strings_);
            isogl_vbo_array_free(&parts_);
            isogl_symbol_cleanup();
            isogl_font_free_data(font_);
            isogl_cleanup();
            ready_ = false;
        }
        doneCurrent();
    }

protected:
    void initializeGL() override {
        if (!gladLoadGLLoader((GLADloadproc)qt_gl_loader)) {
            qCritical("gladLoadGLLoader failed in the Qt6 host");
            return;
        }
        if (isogl_init_with_asset_dir(nullptr) != 0) {
            qCritical("isogl_init failed (set ISOGL_ASSET_DIR)");
            return;
        }

        char font_path[4096];
        if (isogl_asset_path("fonts/ReliefSingleLineLinear.toml",
                             font_path, sizeof(font_path)) != 0) {
            qCritical("font asset path failed");
            return;
        }
        font_ = isogl_font_load_from_toml(font_path);
        if (!font_) {
            qCritical("font load failed");
            return;
        }
        isogl_font_init_vao();
        if (isogl_symbol_init(font_) != 0) {
            qCritical("isogl_symbol_init failed");
            return;
        }

        isogl_set_sheet(210.0f, 297.0f, (isogl_scale){1, 2});
        isogl_zoom_all_sheet();
        build_scene();
        ready_ = true;
    }

    void resizeGL(int w, int h) override {
        (void)w;
        (void)h;
        /* The viewport is recomputed in paintGL: Qt resets it before paint. */
    }

    void paintGL() override {
        int fbw = qRound(width() * devicePixelRatioF());
        int fbh = qRound(height() * devicePixelRatioF());
        if (!ready_ || fbw <= 0 || fbh <= 0) return;

        isogl_viewport vp = {0, 0, fbw, fbh};
        isogl_set_viewport(&vp);
        isogl_begin_frame();

        isogl_clear(0.08f, 0.09f, 0.10f, 1.0f);
        isogl_draw_sheet();
        isogl_draw_all_vbo_objects(&parts_);
        float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        isogl_draw_string_list(&strings_, isogl_shader_program_font,
                               isogl_model_matrix, isogl_view_matrix, identity);
        isogl_symbol_draw_all();
        /* No swap: QOpenGLWidget composites its own FBO. */
    }

    void mousePressEvent(QMouseEvent *event) override {
        QPointF p = event->position() * devicePixelRatioF();
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            last_ = p;
        } else if (event->button() == Qt::LeftButton ||
                   event->button() == Qt::RightButton) {
            pick_at(p);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::MiddleButton) panning_ = false;
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        QPointF p = event->position() * devicePixelRatioF();
        if (panning_) {
            isogl_handle_pan(float(p.x() - last_.x()),
                             float(p.y() - last_.y()));
            last_ = p;
            update();
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        QPointF p = event->position() * devicePixelRatioF();
        int wheel = event->angleDelta().y() > 0 ? 1 : -1;
        isogl_handle_zoom(wheel, float(p.x()), float(p.y()));
        update();
    }

private:
    void pick_at(const QPointF &p) {
        uint32_t ids[4096];
        size_t count = 0;
        int extent = int(std::lround(ISOGL_PICK_DEFAULT_EXTENT *
                                     devicePixelRatioF()));
        if (extent <= 0) extent = ISOGL_PICK_DEFAULT_EXTENT;

        if (isogl_pick_at(&parts_, &strings_, float(p.x()), float(p.y()),
                          extent, ids, sizeof(ids) / sizeof(ids[0]),
                          &count) == 0) {
            qDebug("pick at (%.1f, %.1f): %zu id(s)", p.x(), p.y(), count);
        }

        uint32_t symbol_ids[64];
        size_t symbol_count = 0;
        if (isogl_symbol_pick_at(float(p.x()), float(p.y()), extent,
                                 symbol_ids, 64, &symbol_count) == 0 &&
            symbol_count > 0) {
            qDebug("symbol pick at (%.1f, %.1f): %zu id(s)",
                   p.x(), p.y(), symbol_count);
        }
    }

    void build_scene() {
        isogl_segment_list list;
        if (isogl_segment_list_init(&list, 32) != 0) return;

        const float m = 10.0f, w = 420.0f, h = 594.0f;
        add_line(&parts_, &list, m, m, w - m, m,
                 LINE_THICKNESS_0_7, LINE_TYPE_CONTINUOUS);
        add_line(&parts_, &list, w - m, m, w - m, h - m,
                 LINE_THICKNESS_0_7, LINE_TYPE_CONTINUOUS);
        add_line(&parts_, &list, w - m, h - m, m, h - m,
                 LINE_THICKNESS_0_7, LINE_TYPE_CONTINUOUS);
        add_line(&parts_, &list, m, h - m, m, m,
                 LINE_THICKNESS_0_7, LINE_TYPE_CONTINUOUS);
        add_line(&parts_, &list, m + 20.0f, m + 20.0f, w - m - 20.0f, h - m - 20.0f,
                 LINE_THICKNESS_0_35, LINE_TYPE_DASHED);

        isogl_vbo_array_init(&parts_);
        isogl_vbo_array_add(&parts_, &list, ISOGL_VBO_SEGMENT);
        isogl_segment_list_free(&list);

        isogl_glyph_string *text =
            isogl_create_glyph_string("isogl + Qt6", font_);
        if (text) {
            isogl_string_list_append(&strings_, text, 30.0f, 560.0f, 10.0f,
                                     0.5f, 0.0f, font_, 0);
        }
    }

    isogl_font_data *font_ = nullptr;
    isogl_vbo_array parts_{};
    isogl_string_list strings_{};
    bool ready_ = false;
    bool panning_ = false;
    QPointF last_;
};

int main(int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    CadView view;
    view.setWindowTitle("isogl Qt6 host");
    view.resize(1000, 720);
    view.show();
    return app.exec();
}
