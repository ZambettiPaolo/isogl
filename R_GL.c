#include "R_GL.h"
#include <stdio.h>
#include <stddef.h>

// Variabili private per la gestione della trasformazione
static float zoom_all = 0.0f;
static R_GL_Point pan_all = {0.0f, 0.0f};

// Variabili private per i buffer OpenGL
static GLuint VAO_sheet = 0;
static GLuint VBO_sheet = 0;
static GLuint VAO_geometry = 0;
static GLuint VBO_geometry = 0;
static GLuint EBO_geometry = 0;

// Variabili private per gli shader programs
static GLuint shader_program_segment = 0;
static GLuint shader_program_sheet = 0;

// Matrici di trasformazione
static float model_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

static float view_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

static float projection_matrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, -1, 0,
    0, 0, 0, 1
};

// Shader sources
const char* R_GL_vertex_shader_source_sheet =
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"\n"
"uniform mat4 uProjection;\n"
"uniform mat4 uView;\n"
"uniform mat4 uModel;\n"
"void main()\n"
"{\n"
"    gl_Position = uProjection * uView * uModel * vec4(aPos, 0.0, 1.0);\n"
"}\n";

const char* R_GL_fragment_shader_source_sheet =
"#version 330 core\n"
"out vec4 FragColor;\n"
"\n"
"uniform vec4 uColor;\n"
"void main()\n"
"{\n"
"    FragColor = uColor;\n"
"}\n";

const char* R_GL_vertex_shader_source_segment = 
"#version 330 core\n"
"\n"
"layout(location = 0) in vec3 aPosition;\n"
"layout(location = 1) in vec4 aColor;\n"
"layout(location = 2) in int aPatternType;\n"
"layout(location = 3) in float aSegmentLength;\n"
"layout(location = 4) in float aDistanceFromStart;\n"
"\n"
"flat out vec4 vColor;\n"
"out float vDistance;\n"
"flat out int vPatternType;\n"
"flat out float vSegmentLength;\n"
"\n"
"uniform mat4 uProjection;\n"
"uniform mat4 uView;\n"
"uniform mat4 uModel;\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);\n"
"    vColor = aColor;\n"
"    vDistance = aDistanceFromStart;\n"
"    vPatternType = aPatternType;\n"
"    vSegmentLength = aSegmentLength;\n"
"}\n";

const char* R_GL_fragment_shader_source_segment = 
"#version 330 core\n"
"\n"
"flat in vec4 vColor;\n"
"in float vDistance;\n"
"flat in int vPatternType;\n"
"flat in float vSegmentLength;\n"
"\n"
"out vec4 FragColor;\n"
"\n"
"uniform float uPatternScale;\n"
"\n"
"void main()\n"
"{\n"
"    float alpha = 1.0;\n"
"    \n"
"    if (vPatternType == 1) {\n"
"        float pattern = mod(vDistance, 0.2);\n"
"        if (vDistance > 0.5) {\n"
"           discard; \n"
"        }\n"
"    }\n"
"    else if (vPatternType == 2) {\n"
"        float pattern = mod(vDistance * uPatternScale, 0.2);\n"
"        if ((pattern > 0.08 && pattern < 0.1) || pattern > 0.12) {\n"
"            discard;\n"
"        }\n"
"    }\n"
"    else if (vPatternType == 3) {\n"
"        float pattern = mod(vDistance * uPatternScale, 0.1);\n"
"        if (pattern > 0.02) {\n"
"            discard;\n"
"        }\n"
"    }\n"
"    else if (vPatternType == 4) {\n"
"        alpha = 1.0 - (vDistance / vSegmentLength);\n"
"    }\n"
"    \n"
"    FragColor = vec4(vColor.rgb, vColor.a * alpha);\n"
"}\n";

// Funzioni di inizializzazione
int R_GL_init(void) {
    // Abilita linee antialiasing
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    
    // Crea shader programs
    shader_program_segment = R_GL_create_shader_program(
        R_GL_vertex_shader_source_segment, 
        R_GL_fragment_shader_source_segment
    );
    
    shader_program_sheet = R_GL_create_shader_program(
        R_GL_vertex_shader_source_sheet, 
        R_GL_fragment_shader_source_sheet
    );
    
    if (shader_program_segment == 0 || shader_program_sheet == 0) {
        return -1;
    }
    
    return 0;
}

void R_GL_cleanup(void) {
    if (VAO_geometry) glDeleteVertexArrays(1, &VAO_geometry);
    if (VBO_geometry) glDeleteBuffers(1, &VBO_geometry);
    if (EBO_geometry) glDeleteBuffers(1, &EBO_geometry);
    if (VAO_sheet) glDeleteVertexArrays(1, &VAO_sheet);
    if (VBO_sheet) glDeleteBuffers(1, &VBO_sheet);
    if (shader_program_sheet) glDeleteProgram(shader_program_sheet);
    if (shader_program_segment) glDeleteProgram(shader_program_segment);
}

// Gestione shader
GLuint R_GL_compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Errore compilazione shader: %s\n", infoLog);
        return 0;
    }
    
    return shader;
}

GLuint R_GL_create_shader_program(const char *vertex_shader_source, const char *fragment_shader_source) {
    GLuint vertexShader = R_GL_compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragmentShader = R_GL_compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    if (vertexShader == 0 || fragmentShader == 0) {
        return 0;
    }
    
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "ERROR: Shader linking failed\n%s\n", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return shaderProgram;
}

// Gestione sheet
int R_GL_set_sheet(float width, float height) {
    float vertices[10] = {
        0,      0,
        width,  0,
        width,  height,
        0,      height,
        0,      0
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
        zoom_all = 2.0f * 0.8f / width;
    } else {
        zoom_all = 2.0f * 0.8f / height;
    }
    
    pan_all.x = -zoom_all * width / 2.0f;
    pan_all.y = -zoom_all * height / 2.0f;
    
    return 0;
}

void R_GL_draw_sheet(void) {
    glUseProgram(shader_program_sheet);
    
    GLint projLoc = glGetUniformLocation(shader_program_sheet, "uProjection");
    GLint viewLoc = glGetUniformLocation(shader_program_sheet, "uView");
    GLint modelLoc = glGetUniformLocation(shader_program_sheet, "uModel");
    GLint colorLoc = glGetUniformLocation(shader_program_sheet, "uColor");
    
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection_matrix);
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view_matrix);
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model_matrix);
    
    glBindVertexArray(VAO_sheet);
    
    // Fill
    float color[4] = {0.10f, 0.110f, 0.12f, 1.0f};
    if (colorLoc != -1) glUniform4fv(colorLoc, 1, color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
    
    // Outline
    color[1] = 0.17f;
    color[2] = 0.2f;
    if (colorLoc != -1) glUniform4fv(colorLoc, 1, color);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    
    glBindVertexArray(0);
}

void R_GL_zoom_all_sheet(void) {

    model_matrix[0] = zoom_all;
    model_matrix[5] = zoom_all;
    model_matrix[12] = pan_all.x;
    model_matrix[13] = pan_all.y;
}

// Gestione geometrie
void R_GL_setup_geometry(R_GL_Vertex* vertices, size_t vertex_count, unsigned int* indices, size_t index_count) {
    glGenVertexArrays(1, &VAO_geometry);
    glGenBuffers(1, &VBO_geometry);
    glGenBuffers(1, &EBO_geometry);
    
    glBindVertexArray(VAO_geometry);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO_geometry);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(R_GL_Vertex), vertices, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_geometry);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(unsigned int), indices, GL_STREAM_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex), (void*)offsetof(R_GL_Vertex, position));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex), (void*)offsetof(R_GL_Vertex, color));
    glEnableVertexAttribArray(1);
    
    glVertexAttribIPointer(2, 1, GL_INT, sizeof(R_GL_Vertex), (void*)offsetof(R_GL_Vertex, patternType));
    glEnableVertexAttribArray(2);
    
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex), (void*)offsetof(R_GL_Vertex, segmentLength));
    glEnableVertexAttribArray(3);
    
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex), (void*)offsetof(R_GL_Vertex, distanceFromStart));
    glEnableVertexAttribArray(4);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void R_GL_draw_geometry(size_t index_count) {
    glUseProgram(shader_program_segment);
    
    GLint projLoc = glGetUniformLocation(shader_program_segment, "uProjection");
    GLint viewLoc = glGetUniformLocation(shader_program_segment, "uView");
    GLint modelLoc = glGetUniformLocation(shader_program_segment, "uModel");
    GLint patternScale = glGetUniformLocation(shader_program_segment, "uPatternScale");
    
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection_matrix);
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view_matrix);
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model_matrix);
    if (patternScale != -1) glUniform1f(patternScale, 1.0f);
    
    glBindVertexArray(VAO_geometry);
    glDrawElements(GL_LINES, index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Gestione viewport e trasformazioni
void R_GL_resize_viewport(SDL_Window* window) {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);
    
    if (w >= h) {
        view_matrix[0] = (float)h / (float)w;
        view_matrix[5] = 1.0f;
    } else {
        view_matrix[0] = 1.0f;
        view_matrix[5] = (float)w / (float)h;
    }
}

void R_GL_pixel_to_point(SDL_Window *window, R_GL_Point *pixelP, R_GL_Point *modelP) {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    
    if (w >= h) {
        modelP->x = 2.0f * pixelP->x / h - (float)w / h;
        modelP->y = -(2.0f * pixelP->y / h - 1.0f);
    } else {
        modelP->x = 2.0f * pixelP->x / w - 1.0f;
        modelP->y = -(2.0f * pixelP->y / w - (float)h / w);
    }
}

// Gestione zoom e pan
void R_GL_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY) {
    R_GL_Point pixelP = {mouseX, mouseY};
    R_GL_Point modelP;
    R_GL_pixel_to_point(window, &pixelP, &modelP);
   	float zoom = model_matrix[0];
	R_GL_Point pan = {model_matrix[12], model_matrix[13]};
    if (wheel_y > 0) {
        zoom *= 2.0f;
        pan.x = pan.x * 2.0f - modelP.x;
        pan.y = pan.y * 2.0f - modelP.y;
    } else if (wheel_y < 0) {
        zoom /= 2.0f;
        pan.x = (pan.x + modelP.x) / 2.0f;
        pan.y = (pan.y + modelP.y) / 2.0f;
    }
    
    model_matrix[0] = zoom;
    model_matrix[5] = zoom;
    model_matrix[12] = pan.x;
    model_matrix[13] = pan.y;
}

void R_GL_handle_pan(SDL_Window* window, int xrel, int yrel) {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    float deltaX, deltaY;
    
    if (w >= h) {
        deltaX = (2.0f * xrel) / h;
        deltaY = -(2.0f * yrel) / h;
    } else {
        deltaX = (2.0f * xrel) / w;
        deltaY = -(2.0f * yrel) / w;
    }
    
    model_matrix[12] += deltaX;
    model_matrix[13] += deltaY;
}

// Rendering
void R_GL_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void R_GL_swap_buffers(SDL_Window* window) {
    SDL_GL_SwapWindow(window);
}
