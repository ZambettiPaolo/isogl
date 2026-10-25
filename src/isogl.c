#include "isogl.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define MAX_DIM_SHADER 8192 // Size max of buffer
#define ISOGL_PATH_MAX 4096

// Matrici di trasformazione
float isogl_model_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

float isogl_view_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

static float zoom_all = 0.0f;
static isogl_point pan_all = {0.0f, 0.0f};
static float scale = 1;
static GLint colorThikness = 0;

// Variabili private per i buffer OpenGL
static GLuint VAO_sheet = 0;
static GLuint VBO_sheet = 0;
static GLuint VAO_geometry = 0;

// Variabili private per gli shader programs
GLuint isogl_shader_program_segment = 0;
GLuint isogl_shader_program_sheet = 0;
GLuint isogl_shader_program_font = 0;


// ===== CPU STAGING LIST =====

int isogl_segment_list_init(isogl_segment_list *list, size_t initial_capacity) {
    if (!list) return -1;

    size_t capacity = initial_capacity ? initial_capacity : 64;
    if (capacity > SIZE_MAX / sizeof(isogl_vertex)) return -1;

    list->vertex = malloc(capacity * sizeof(isogl_vertex));
    if (!list->vertex) {
        list->size = 0;
        list->capacity = 0;
        return -1;
    }

    list->size = 0;
    list->capacity = capacity;
    return 0;
}

void isogl_segment_list_free(isogl_segment_list *list) {
    if (!list) return;
    free(list->vertex);
    list->vertex = NULL;
    list->size = 0;
    list->capacity = 0;
}

int isogl_segment_list_push_pair(isogl_segment_list *list, isogl_vertex v0, isogl_vertex v1) {
    if (!list) return -1;

    if (list->size + 2 > list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 64;
        if (new_capacity < list->capacity ||
            new_capacity > SIZE_MAX / sizeof(isogl_vertex)) {
            return -1;
        }

        isogl_vertex *grown = realloc(list->vertex,
                                      new_capacity * sizeof(isogl_vertex));
        if (!grown) return -1;

        list->vertex = grown;
        list->capacity = new_capacity;
    }

    list->vertex[list->size++] = v0;
    list->vertex[list->size++] = v1;
    return 0;
}

// ===== ASSET RESOLUTION =====

static char isogl_asset_root[ISOGL_PATH_MAX] = ".";

const char *isogl_asset_dir(void) {
    return isogl_asset_root;
}

int isogl_asset_path(const char *relative, char *out, size_t out_size) {
    if (!relative || !out || out_size == 0) return -1;

    int written = snprintf(out, out_size, "%s/%s", isogl_asset_root, relative);
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

static int isogl_asset_root_has_shaders(const char *root) {
    char probe[ISOGL_PATH_MAX];
    int written = snprintf(probe, sizeof(probe), "%s/shaders/vs_segment.glsl", root);
    if (written < 0 || (size_t)written >= sizeof(probe)) return 0;
    return access(probe, R_OK) == 0;
}

static const char *isogl_choose_asset_root(const char *asset_dir) {
    if (asset_dir && *asset_dir) return asset_dir;

    const char *from_env = getenv("ISOGL_ASSET_DIR");
    if (from_env && *from_env) return from_env;

#ifdef ISOGL_DEFAULT_ASSET_DIR
    if (isogl_asset_root_has_shaders(ISOGL_DEFAULT_ASSET_DIR)) {
        return ISOGL_DEFAULT_ASSET_DIR;
    }
#endif

    // Source-tree / development fallback.
    return ".";
}

static int isogl_init_internal(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glGenVertexArrays(1, &VAO_geometry);

    char vs[ISOGL_PATH_MAX];
    char gs[ISOGL_PATH_MAX];
    char fs[ISOGL_PATH_MAX];

    if (isogl_asset_path("shaders/vs_segment.glsl", vs, sizeof(vs)) != 0 ||
        isogl_asset_path("shaders/gs_segment.glsl", gs, sizeof(gs)) != 0 ||
        isogl_asset_path("shaders/fs_segment.glsl", fs, sizeof(fs)) != 0) {
        fprintf(stderr, "isogl: asset path too long (root: %s)\n", isogl_asset_root);
        return -1;
    }

    isogl_shader_program_segment = isogl_create_shader_program(vs, gs, fs);

    if (isogl_asset_path("shaders/vs_sheet.glsl", vs, sizeof(vs)) != 0 ||
        isogl_asset_path("shaders/fs_sheet.glsl", fs, sizeof(fs)) != 0) {
        fprintf(stderr, "isogl: asset path too long (root: %s)\n", isogl_asset_root);
        return -1;
    }
    isogl_shader_program_sheet = isogl_create_shader_program(vs, NULL, fs);

    if (isogl_asset_path("shaders/vs_font.glsl", vs, sizeof(vs)) != 0 ||
        isogl_asset_path("shaders/fs_font.glsl", fs, sizeof(fs)) != 0) {
        fprintf(stderr, "isogl: asset path too long (root: %s)\n", isogl_asset_root);
        return -1;
    }
    isogl_shader_program_font = isogl_create_shader_program(vs, NULL, fs);

    if (isogl_shader_program_segment == 0 ||
        isogl_shader_program_sheet == 0 ||
        isogl_shader_program_font == 0) {
        fprintf(stderr, "isogl: failed to load shaders from '%s'\n", isogl_asset_root);
        return -1;
    }

    return 0;
}

int isogl_init_with_asset_dir(const char *asset_dir) {
    const char *root = isogl_choose_asset_root(asset_dir);

    int written = snprintf(isogl_asset_root, sizeof(isogl_asset_root), "%s", root);
    if (written < 0 || (size_t)written >= sizeof(isogl_asset_root)) {
        fprintf(stderr, "isogl: asset directory path too long\n");
        return -1;
    }

    return isogl_init_internal();
}

int isogl_init(void) {
    return isogl_init_with_asset_dir(NULL);
}

void isogl_cleanup(void) {
    // Mesa keeps the last program used for drawing alive until another
    // program is bound, even after glUseProgram(0).  Binding the segment
    // program releases the reference so the font program can be deleted.
    glUseProgram(0);
    if (isogl_shader_program_segment) {
        glUseProgram(isogl_shader_program_segment);
        glUseProgram(0);
    }

    if (VAO_geometry) glDeleteVertexArrays(1, &VAO_geometry);
    if (VAO_sheet) glDeleteVertexArrays(1, &VAO_sheet);
    if (VBO_sheet) glDeleteBuffers(1, &VBO_sheet);
    if (isogl_shader_program_sheet) glDeleteProgram(isogl_shader_program_sheet);
    if (isogl_shader_program_segment) glDeleteProgram(isogl_shader_program_segment);
    if (isogl_shader_program_font) glDeleteProgram(isogl_shader_program_font);

    isogl_font_cleanup();

    VAO_geometry = 0;
    VAO_sheet = 0;
    VBO_sheet = 0;
    isogl_shader_program_sheet = 0;
    isogl_shader_program_segment = 0;
    isogl_shader_program_font = 0;
}

// ===== GESTIONE SHADER =====

GLuint isogl_load_shader(GLenum shader_type, char *shader_file_name) {
    if (!shader_file_name) return 0;

    FILE *file = fopen(shader_file_name, "rb");
    if (!file) {
        fprintf(stderr, "isogl: cannot open shader '%s'\n", shader_file_name);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long dim = ftell(file);
    if (dim <= 0 || dim >= MAX_DIM_SHADER) {
        fprintf(stderr, "isogl: invalid shader size for '%s'\n", shader_file_name);
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    char *source = malloc((size_t)dim + 1);
    if (!source) {
        fclose(file);
        return 0;
    }

    size_t read_bytes = fread(source, 1, (size_t)dim, file);
    fclose(file);
    if (read_bytes != (size_t)dim) {
        free(source);
        return 0;
    }

    source[dim] = '\0';
    GLuint shader = isogl_compile_shader(shader_type, source);
    free(source);
    return shader;
}

GLuint isogl_compile_shader(GLenum type, char* source) {
    if (!source) return 0;

    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;

    const GLchar *text = source;
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "Shader compile error: %s\n", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint isogl_create_shader_program(
    char *vertex_shader_source,
    char *geometry_shader_source,
    char *fragment_shader_source)
{
    GLuint vertexShader = isogl_load_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint geometryShader = 0;
    GLuint fragmentShader = isogl_load_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    if (geometry_shader_source) {
        geometryShader = isogl_load_shader(GL_GEOMETRY_SHADER, geometry_shader_source);
    }

    if (vertexShader == 0 || fragmentShader == 0 ||
        (geometry_shader_source && geometryShader == 0)) {
        fprintf(stderr, "Shader compilation failed\n");
        if (vertexShader) glDeleteShader(vertexShader);
        if (geometryShader) glDeleteShader(geometryShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        if (vertexShader) glDeleteShader(vertexShader);
        if (geometryShader) glDeleteShader(geometryShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return 0;
    }

    glAttachShader(program, vertexShader);
    if (geometry_shader_source)
        glAttachShader(program, geometryShader);
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        if (geometry_shader_source) glDeleteShader(geometryShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    if (geometry_shader_source) glDeleteShader(geometryShader);
    glDeleteShader(fragmentShader);

    return program;
}

// ===== GESTIONE SHEET =====

int isogl_set_sheet(float width, float height, isogl_scale sc) {
    if (width <= 0.0f || height <= 0.0f || sc.den == 0) return -1;

    float new_scale = (float)sc.num / (float)sc.den;
    if (new_scale <= 0.0f) return -1;
    scale = new_scale;

    if (VAO_sheet) {
        glDeleteVertexArrays(1, &VAO_sheet);
        VAO_sheet = 0;
    }
    if (VBO_sheet) {
        glDeleteBuffers(1, &VBO_sheet);
        VBO_sheet = 0;
    }

    float vertices[10] = {
        0,            0,
        width/scale,  0,
        width/scale,  height/scale,
        0,            height/scale,
        0,            0
    };

    glGenVertexArrays(1, &VAO_sheet);
    glGenBuffers(1, &VBO_sheet);
    
    glBindVertexArray(VAO_sheet);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_sheet);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    if (width >= height) {
        zoom_all = 2.0f * 0.8f * scale / width;
    } else {
        zoom_all = 2.0f * 0.8f * scale / height;
    }
    
    pan_all.x = -zoom_all * width / (2.0f * scale);
    pan_all.y = -zoom_all * height / (2.0f * scale);
    
    return 0;
}

// Cached sheet uniforms.
typedef struct {
    GLuint program;
    GLint view;
    GLint model;
    GLint color;
} isogl_sheet_uniforms;

static isogl_sheet_uniforms isogl_sheet_u;

static void isogl_cache_sheet_uniforms(GLuint program) {
    if (isogl_sheet_u.program == program) return;
    isogl_sheet_u.program = program;
    isogl_sheet_u.view = glGetUniformLocation(program, "uView");
    isogl_sheet_u.model = glGetUniformLocation(program, "uModel");
    isogl_sheet_u.color = glGetUniformLocation(program, "uColor");
}

void isogl_draw_sheet(void) {
    if (VAO_sheet == 0) return;

    glUseProgram(isogl_shader_program_sheet);
    isogl_cache_sheet_uniforms(isogl_shader_program_sheet);

    if (isogl_sheet_u.view != -1)
        glUniformMatrix4fv(isogl_sheet_u.view, 1, GL_FALSE, isogl_view_matrix);
    if (isogl_sheet_u.model != -1)
        glUniformMatrix4fv(isogl_sheet_u.model, 1, GL_FALSE, isogl_model_matrix);

    glBindVertexArray(VAO_sheet);

    // Fill
    float color[4] = {0.10f, 0.110f, 0.12f, 1.0f};
    if (isogl_sheet_u.color != -1) glUniform4fv(isogl_sheet_u.color, 1, color);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Outline
    color[1] = 0.17f;
    color[2] = 0.2f;
    if (isogl_sheet_u.color != -1) glUniform4fv(isogl_sheet_u.color, 1, color);
    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glBindVertexArray(0);
    glUseProgram(0);
}

void isogl_zoom_all_sheet(void) {
    isogl_model_matrix[0] = zoom_all;
    isogl_model_matrix[5] = zoom_all;
    isogl_model_matrix[12] = pan_all.x;
    isogl_model_matrix[13] = pan_all.y;
}

void isogl_toggle_color_thickness(void) {
    colorThikness = (colorThikness == 0) ? 1 : 0;
}

// ===== GESTIONE VIEWPORT E TRASFORMAZIONI =====

void isogl_resize_viewport(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);

    if (w <= 0 || h <= 0) return;

    if (w >= h) {
        isogl_view_matrix[0] = (float)h / (float)w;
        isogl_view_matrix[5] = 1.0f;
    } else {
        isogl_view_matrix[0] = 1.0f;
        isogl_view_matrix[5] = (float)w / (float)h;
    }
}

// Returns normalised device coordinates (NDC), not world coordinates.
void isogl_pixel_to_point(SDL_Window *window, isogl_point *pixelP, isogl_point *modelP) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);

    if (w <= 0 || h <= 0) {
        modelP->x = 0.0f;
        modelP->y = 0.0f;
        return;
    }

    if (w >= h) {
        modelP->x = 2.0f * pixelP->x / h - (float)w / h;
        modelP->y = -(2.0f * pixelP->y / h - 1.0f);
    } else {
        modelP->x = 2.0f * pixelP->x / w - 1.0f;
        modelP->y = -(2.0f * pixelP->y / w - (float)h / w);
    }
}

int isogl_ndc_to_world(float ndc_x, float ndc_y, isogl_point *world) {
    if (!world) return -1;

    float ax = isogl_view_matrix[0];
    float ay = isogl_view_matrix[5];
    float zoom_x = isogl_model_matrix[0];
    float zoom_y = isogl_model_matrix[5];

    if (fabsf(ax) < 1e-8f || fabsf(ay) < 1e-8f ||
        fabsf(zoom_x) < 1e-8f || fabsf(zoom_y) < 1e-8f) {
        return -1;
    }

    world->x = (ndc_x / ax - isogl_model_matrix[12]) / zoom_x;
    world->y = (ndc_y / ay - isogl_model_matrix[13]) / zoom_y;
    return 0;
}

int isogl_screen_to_world(SDL_Window *window, float px, float py, isogl_point *world) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return -1;

    float ndc_x = 2.0f * px / (float)w - 1.0f;
    float ndc_y = 1.0f - 2.0f * py / (float)h;
    return isogl_ndc_to_world(ndc_x, ndc_y, world);
}

// ===== GESTIONE ZOOM E PAN =====

void isogl_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY) {
    isogl_point pixelP = {mouseX, mouseY};
    isogl_point ndc;
    isogl_pixel_to_point(window, &pixelP, &ndc);

    float zoom_old = isogl_model_matrix[0];
    float zoom_new = zoom_old;

    if (wheel_y > 0)
        zoom_new *= 1.41f;
    else if (wheel_y < 0)
        zoom_new /= 1.41f;
    else
        return;

    float ax = isogl_view_matrix[0];
    float ay = isogl_view_matrix[5];
    if (fabsf(zoom_old) < 1e-8f || fabsf(ax) < 1e-8f || fabsf(ay) < 1e-8f) {
        return;
    }

    float factor = zoom_new / zoom_old;

    isogl_point pan;
    pan.x = isogl_model_matrix[12];
    pan.y = isogl_model_matrix[13];

    // The cursor anchor is in NDC: convert it to model space by dividing out
    // the aspect scale, then keep it fixed while zooming.
    float anchor_x = ndc.x / ax;
    float anchor_y = ndc.y / ay;

    pan.x = factor * pan.x + (1.0f - factor) * anchor_x;
    pan.y = factor * pan.y + (1.0f - factor) * anchor_y;

    isogl_model_matrix[0] = zoom_new;
    isogl_model_matrix[5] = zoom_new;
    isogl_model_matrix[12] = pan.x;
    isogl_model_matrix[13] = pan.y;
}

void isogl_handle_pan(SDL_Window* window, int xrel, int yrel) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) return;

    float deltaX, deltaY;

    if (w >= h) {
        deltaX = (2.0f * xrel) / h;
        deltaY = -(2.0f * yrel) / h;
    } else {
        deltaX = (2.0f * xrel) / w;
        deltaY = -(2.0f * yrel) / w;
    }

    isogl_model_matrix[12] += deltaX;
    isogl_model_matrix[13] += deltaY;
}

// ===== RENDERING =====

void isogl_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void isogl_swap_buffers(SDL_Window* window) {
    SDL_GL_SwapWindow(window);
}

//======================================================================================================
/**
 * @brief Initialize the VBO array
 * 
 * @param vbo_array Pointer to the VBO array to initialize
 * @return 0 on success, -1 on allocation error
 */
int isogl_vbo_array_init(isogl_vbo_array *vbo_array) {
    if (!vbo_array) return -1;

    vbo_array->size = 0;
    vbo_array->capacity = 10;
    vbo_array->objects = calloc(vbo_array->capacity, sizeof(isogl_vbo_object));

    if (!vbo_array->objects) {
        vbo_array->capacity = 0;
        fprintf(stderr, "isogl: failed to allocate VBO array\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Create a new VBO, upload segment data, and add to array
 *
 * @param vbo_array Pointer to the VBO array
 * @param segment_list Segment data to upload
 * @param type Type of VBO; only ISOGL_VBO_SEGMENT is implemented
 * @return VBO ID on success, 0 on error
 */
GLuint isogl_vbo_array_add(isogl_vbo_array *vbo_array,
                           const isogl_segment_list *segment_list,
                           isogl_vbo_type type) {
    if (!vbo_array || !segment_list || !segment_list->vertex ||
        segment_list->size == 0 || (segment_list->size & 1u)) {
        fprintf(stderr, "isogl: invalid parameters for isogl_vbo_array_add\n");
        return 0;
    }

    if (type != ISOGL_VBO_SEGMENT) {
        fprintf(stderr, "isogl: VBO type %d is not implemented\n", (int)type);
        return 0;
    }

    // Grow the object array if needed.
    if (vbo_array->size >= vbo_array->capacity) {
        size_t new_capacity = vbo_array->capacity ? vbo_array->capacity * 2 : 10;
        if (new_capacity < vbo_array->capacity ||
            new_capacity > SIZE_MAX / sizeof(isogl_vbo_object)) {
            return 0;
        }

        isogl_vbo_object *grown =
            realloc(vbo_array->objects, new_capacity * sizeof(isogl_vbo_object));
        if (!grown) {
            fprintf(stderr, "isogl: failed to expand VBO array\n");
            return 0;
        }
        vbo_array->objects = grown;
        vbo_array->capacity = new_capacity;
    }

    GLuint vbo_id = 0;
    glGenBuffers(1, &vbo_id);
    if (vbo_id == 0) {
        fprintf(stderr, "isogl: failed to generate VBO\n");
        return 0;
    }

    size_t bytes = segment_list->size * sizeof(isogl_vertex);

    // Drain pre-existing errors so the upload check below is meaningful.
    while (glGetError() != GL_NO_ERROR) {}

    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, segment_list->vertex,
                 GL_DYNAMIC_DRAW);

    GLint buffer_size = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    GLenum err = glGetError();

    if (err != GL_NO_ERROR || (size_t)buffer_size < bytes) {
        fprintf(stderr, "isogl: VBO upload failed (GL error 0x%x, size %d)\n",
                err, buffer_size);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &vbo_id);
        return 0;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    size_t index = vbo_array->size;
    vbo_array->objects[index].vbo_id = vbo_id;
    vbo_array->objects[index].vertex_count = segment_list->size;
    vbo_array->objects[index].visible = 1;
    vbo_array->objects[index].type = type;

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    memcpy(vbo_array->objects[index].transform, identity, sizeof(identity));

    vbo_array->size++;

    return vbo_id;
}

/**
 * @brief Re-upload the contents of one part without touching the others.
 *
 * Called by the application when a part is marked modified.  The VBO keeps its
 * identity; only its contents (and vertex count) change.
 *
 * @return 0 on success, -1 if the part is not found or the upload fails.
 */
int isogl_vbo_array_update(isogl_vbo_array *vbo_array, GLuint id,
                           const isogl_segment_list *segment_list) {
    if (!vbo_array || !segment_list || !segment_list->vertex ||
        segment_list->size == 0 || (segment_list->size & 1u) || id == 0) {
        return -1;
    }

    for (size_t i = 0; i < vbo_array->size; ++i) {
        if (vbo_array->objects[i].vbo_id != id) continue;

        size_t bytes = segment_list->size * sizeof(isogl_vertex);

        while (glGetError() != GL_NO_ERROR) {}

        glBindBuffer(GL_ARRAY_BUFFER, id);

        GLint current_size = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &current_size);

        if (current_size > 0 && (size_t)current_size >= bytes) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes,
                            segment_list->vertex);
        } else {
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, segment_list->vertex,
                         GL_DYNAMIC_DRAW);
        }

        GLenum err = glGetError();
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (err != GL_NO_ERROR) {
            fprintf(stderr, "isogl: VBO update failed (GL error 0x%x)\n", err);
            return -1;
        }

        vbo_array->objects[i].vertex_count = segment_list->size;
        return 0;
    }

    return -1;
}

/**
 * @brief Set transformation using TRS parameters
 * 
 * @param vbo_array Pointer to the VBO array
 * @param id VBO ID
 * @param tx Translation X
 * @param ty Translation Y
 * @param rotation Rotation in radians
 * @param scale Scale factor
 * @return 0 on success, -1 if index out of bounds
 */
int isogl_vbo_array_set_transform(isogl_vbo_array *vbo_array, GLuint id, 
                                  float tx, float ty, float rotation, float scale) {
    if (vbo_array && id > 0) {
        float cosr = cosf(rotation);
        float sinr = sinf(rotation);
        float transform_matrix[16] = {
            scale * cosr,  scale * sinr,  0.0f, 0.0f,    // colonna 0
            -scale * sinr, scale * cosr,  0.0f, 0.0f,    // colonna 1
            0.0f,          0.0f,          1.0f, 0.0f,    // colonna 2
            tx,            ty,            0.0f, 1.0f     // colonna 3 (traslazione)
        };

        for (size_t index = 0; index < vbo_array->size; index++) {
            if (vbo_array->objects[index].vbo_id == id) {
                for (int i = 0; i < 16; ++i)
                    vbo_array->objects[index].transform[i] = transform_matrix[i];
                return 0;
            }
        }
    }
    printf("Error: VBO id:%u not found ", id);
    return -1;
}

/**
 * @brief Set visibility for a VBO object
 * 
 * @param vbo_array Pointer to the VBO array
 * @param id VBO ID
 * @param visible Visibility flag (0 = hidden, 1 = visible)
 * @return 0 on success, -1 if index out of bounds
 */
int isogl_vbo_array_set_visible(isogl_vbo_array *vbo_array, GLuint id, int visible) {
    if (vbo_array && id > 0) {
        for (size_t index = 0; index < vbo_array->size; index++) {
            if (vbo_array->objects[index].vbo_id == id) {
                vbo_array->objects[index].visible = visible;
                return 0;
            }
        }
    }
    printf("Error: VBO id:%u not found ", id);
    return -1;
}

/**
 * @brief Get VBO object by ID
 * 
 * @param vbo_array Pointer to the VBO array
 * @param id VBO ID
 * @return Pointer to isogl_vbo_object, or NULL if not found
 */
isogl_vbo_object* isogl_vbo_array_get(isogl_vbo_array *vbo_array, GLuint id) {
    if (vbo_array && id > 0) {
        for (size_t index = 0; index < vbo_array->size; index++) {
            if (vbo_array->objects[index].vbo_id == id) {
                return &vbo_array->objects[index];
            }
        }
    }
    printf("Error: VBO id:%u not found ", id);
    return NULL;
}

/**
 * @brief Remove a VBO object from the array
 * 
 * @param vbo_array Pointer to the VBO array
 * @param id VBO ID to remove
 * @return 0 on success, -1 on error
 */
int isogl_vbo_array_remove(isogl_vbo_array *vbo_array, GLuint id) {
    // Validate parameters
    if (!vbo_array || id == 0) {
        printf("Error: Invalid parameters for isogl_vbo_array_remove\n");
        return -1;
    }
    
    // Find the VBO with matching id
    for (size_t i = 0; i < vbo_array->size; i++) {
        if (vbo_array->objects[i].vbo_id == id) {
            // Delete the OpenGL VBO
            glDeleteBuffers(1, &vbo_array->objects[i].vbo_id);
            
            // Shift remaining elements left
            for (size_t j = i; j < vbo_array->size - 1; j++) {
                vbo_array->objects[j] = vbo_array->objects[j + 1];
            }
            
            vbo_array->size--;
            
            printf("Removed VBO %u from index %zu\n", id, i);
            return 0;
        }
    }
    
    // VBO not found
    printf("Error: VBO id %u not found\n", id);
    return -1;
}

/**
 * @brief Free all resources used by the VBO array
 * 
 * @param vbo_array Pointer to the VBO array
 */
void isogl_vbo_array_free(isogl_vbo_array *vbo_array) {
    if (!vbo_array) return;
    
    // Delete all OpenGL VBOs
    for (size_t i = 0; i < vbo_array->size; i++) {
        glDeleteBuffers(1, &vbo_array->objects[i].vbo_id);
    }
    
    // Free the array
    if (vbo_array->objects) {
        free(vbo_array->objects);
        vbo_array->objects = NULL;
    }
    
    vbo_array->size = 0;
    vbo_array->capacity = 0;
}

/**
 * @brief Draw a specific VBO object with its transformation matrix
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object to draw
 */
// Cached uniform locations: resolved once per program, not per draw.
typedef struct {
    GLuint program;
    GLint view;
    GLint model;
    GLint relative;
    GLint color_thickness;
    GLint scale;
    GLint thickness_mask;
    GLint pattern_mask;
    GLint layer_mask;
    GLint weight_color;
    GLint weight_mm;
    GLint pattern_ratios;
    GLint pattern_count;
} isogl_segment_uniforms;

static isogl_segment_uniforms isogl_seg_uniforms;
static float isogl_pattern_ratios_flat[LINE_TYPE_COUNT * ISOGL_MAX_PATTERN_RATIOS];
static int isogl_pattern_counts[LINE_TYPE_COUNT];
static int isogl_style_uniforms_ready;

static void isogl_prepare_style_uniforms(void) {
    if (isogl_style_uniforms_ready) return;

    for (int i = 0; i < LINE_TYPE_COUNT; ++i) {
        isogl_pattern_counts[i] = isogl_line_patterns[i].count;
        for (int j = 0; j < ISOGL_MAX_PATTERN_RATIOS; ++j) {
            isogl_pattern_ratios_flat[i * ISOGL_MAX_PATTERN_RATIOS + j] =
                isogl_line_patterns[i].ratios[j];
        }
    }
    isogl_style_uniforms_ready = 1;
}

static void isogl_cache_segment_uniforms(GLuint program) {
    if (isogl_seg_uniforms.program == program) return;

    isogl_seg_uniforms.program = program;
    isogl_seg_uniforms.view = glGetUniformLocation(program, "uView");
    isogl_seg_uniforms.model = glGetUniformLocation(program, "uModel");
    isogl_seg_uniforms.relative = glGetUniformLocation(program, "uRelative");
    isogl_seg_uniforms.color_thickness = glGetUniformLocation(program, "uColorThickness");
    isogl_seg_uniforms.scale = glGetUniformLocation(program, "uScale");
    isogl_seg_uniforms.thickness_mask = glGetUniformLocation(program, "uThicknessMask");
    isogl_seg_uniforms.pattern_mask = glGetUniformLocation(program, "uPatternMask");
    isogl_seg_uniforms.layer_mask = glGetUniformLocation(program, "uLayerMask");
    isogl_seg_uniforms.weight_color = glGetUniformLocation(program, "uWeightColor");
    isogl_seg_uniforms.weight_mm = glGetUniformLocation(program, "uWeightMM");
    isogl_seg_uniforms.pattern_ratios = glGetUniformLocation(program, "uPatternRatios");
    isogl_seg_uniforms.pattern_count = glGetUniformLocation(program, "uPatternCount");
}

void isogl_draw_vbo_object(isogl_vbo_array *vbo_array, size_t index) {
    if (!vbo_array || index >= vbo_array->size) return;

    isogl_vbo_object *obj = &vbo_array->objects[index];
    if (!obj->visible) return;

    if (obj->type != ISOGL_VBO_SEGMENT) {
        return;
    }
    if (isogl_shader_program_segment == 0) return;

    GLuint current_shader = isogl_shader_program_segment;
    glUseProgram(current_shader);

    isogl_cache_segment_uniforms(current_shader);
    isogl_prepare_style_uniforms();

    if (isogl_seg_uniforms.view != -1)
        glUniformMatrix4fv(isogl_seg_uniforms.view, 1, GL_FALSE, isogl_view_matrix);
    if (isogl_seg_uniforms.model != -1)
        glUniformMatrix4fv(isogl_seg_uniforms.model, 1, GL_FALSE, isogl_model_matrix);
    if (isogl_seg_uniforms.relative != -1)
        glUniformMatrix4fv(isogl_seg_uniforms.relative, 1, GL_FALSE, obj->transform);
    if (isogl_seg_uniforms.color_thickness != -1)
        glUniform1i(isogl_seg_uniforms.color_thickness, colorThikness);
    if (isogl_seg_uniforms.scale != -1)
        glUniform1f(isogl_seg_uniforms.scale, scale);

    if (isogl_seg_uniforms.weight_color != -1) {
        glUniform4fv(isogl_seg_uniforms.weight_color, LINE_THICKNESS_COUNT,
                     &isogl_weight_color[0][0]);
    }
    if (isogl_seg_uniforms.weight_mm != -1) {
        glUniform1fv(isogl_seg_uniforms.weight_mm, LINE_THICKNESS_COUNT,
                     isogl_line_weight_mm);
    }
    if (isogl_seg_uniforms.pattern_ratios != -1) {
        glUniform1fv(isogl_seg_uniforms.pattern_ratios,
                     LINE_TYPE_COUNT * ISOGL_MAX_PATTERN_RATIOS,
                     isogl_pattern_ratios_flat);
    }
    if (isogl_seg_uniforms.pattern_count != -1) {
        glUniform1iv(isogl_seg_uniforms.pattern_count, LINE_TYPE_COUNT,
                     isogl_pattern_counts);
    }

    unsigned int all_masks = 0xFFFFFFFFu;
    if (isogl_seg_uniforms.thickness_mask != -1)
        glUniform1ui(isogl_seg_uniforms.thickness_mask, all_masks);
    if (isogl_seg_uniforms.pattern_mask != -1)
        glUniform1ui(isogl_seg_uniforms.pattern_mask, all_masks);
    if (isogl_seg_uniforms.layer_mask != -1)
        glUniform1ui(isogl_seg_uniforms.layer_mask, all_masks);

    glBindVertexArray(VAO_geometry);
    glBindBuffer(GL_ARRAY_BUFFER, obj->vbo_id);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(isogl_vertex),
                          (void*)offsetof(isogl_vertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(isogl_vertex),
                          (void*)offsetof(isogl_vertex, color));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(isogl_vertex),
                          (void*)offsetof(isogl_vertex, distanceFromStart));
    glEnableVertexAttribArray(2);

    glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(isogl_vertex),
                           (void*)offsetof(isogl_vertex, pk_thickness_pattern_layer));
    glEnableVertexAttribArray(3);

    glDrawArrays(GL_LINES, 0, (GLsizei)obj->vertex_count);

    glBindVertexArray(0);
    glUseProgram(0);
}

/**
 * @brief Draw all visible VBO objects
 * 
 * @param vbo_array Pointer to the VBO array
 */
void isogl_draw_all_vbo_objects(isogl_vbo_array *vbo_array) {
    if (!vbo_array) return;
    for (size_t i = 0; i < vbo_array->size; i++) {
        isogl_draw_vbo_object(vbo_array, i);
    }
}
