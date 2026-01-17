#include "R_GL.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define MAX_DIM_SHADER 8192 // Size max of buffer

static float R_GL_w = 100;
static float R_GL_h = 100;

// Variabili private per la gestione della trasformazione
static float zoom_all = 0.0f;
static R_GL_Point pan_all = {0.0f, 0.0f};
static float scale = 1;
static GLint colorThikness = 0;

// Variabili private per i buffer OpenGL
static GLuint VAO_sheet = 0;
static GLuint VBO_sheet = 0;
static GLuint VAO_geometry = 0;
static GLuint VBO_geometry = 0;

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

unsigned int R_GL_pack(unsigned int thickness, unsigned int pattern, unsigned int layer){
    return (thickness & 0x1F) |           // 8 bits for thickness (0-255)
           ((pattern & 0x1F) << 5) |      // 8 bits for pattern (0-255)
           ((layer & 0x1F) << 5);        // 8 bits for layer (0-255)
                                          // bits 24-31 reserved for future use
}

unsigned int R_GL_unpack_thickness(unsigned int packed) {
    return packed & 0xFF;
}

unsigned int R_GL_unpack_pattern(unsigned int packed) {
    return (packed >> 8) & 0xFF;
}

unsigned int R_GL_unpack_layer(unsigned int packed) {
    return (packed >> 16) & 0xFF;
}


// this function load the program shader foron local directori and compile int
// return the GLint of program or 0 if an error occurred

GLuint R_GL_load_shader(GLenum shader_type  , char *shader_file_name){
	
	FILE * f_shader = fopen(shader_file_name, "r");
	if (f_shader == NULL ) return 0;
	
	fseek(f_shader, 0, SEEK_END);
    long dim = ftell(f_shader);
    fseek(f_shader, 0, SEEK_SET);
	
	if (dim >= MAX_DIM_SHADER) {
		fclose(f_shader);
		return 0;
	}

	char shader_string[dim+1]; 
	fread(shader_string, 1, dim, f_shader);
    shader_string[dim] = '\0';  // end of string
	fclose(f_shader);
	return R_GL_compile_shader(shader_type, shader_string);
}

// ===== FUNZIONI DI INIZIALIZZAZIONE =====

int R_GL_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    glGenVertexArrays(1, &VAO_geometry);
    glGenBuffers(1, &VBO_geometry); 
    // Crea shader programs
    shader_program_segment = R_GL_create_shader_program(
        "./vs_segment.glsl", 
    	"./gs_segment.glsl",
        "./fs_segment.glsl"
    );
    
    shader_program_sheet = R_GL_create_shader_program(
        "./vs_sheet.glsl", 
        NULL,
        "./fs_sheet.glsl"
    );
    
    if (shader_program_segment == 0 || shader_program_sheet == 0) {
        return -1;
    }
    
    return 0;
}

void R_GL_cleanup(void) {
    if (VAO_geometry) glDeleteVertexArrays(1, &VAO_geometry);
    if (VBO_geometry) glDeleteBuffers(1, &VBO_geometry);
    if (VAO_sheet) glDeleteVertexArrays(1, &VAO_sheet);
    if (VBO_sheet) glDeleteBuffers(1, &VBO_sheet);
    if (shader_program_sheet) glDeleteProgram(shader_program_sheet);
    if (shader_program_segment) glDeleteProgram(shader_program_segment);
}

// ===== GESTIONE SHADER =====

GLuint R_GL_compile_shader(GLenum type, char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1,  (const GLchar**)&source, NULL);
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

GLuint R_GL_create_shader_program(
    char *vertex_shader_source,
    char *geometry_shader_source,
    char *fragment_shader_source)
{
    GLuint vertexShader =R_GL_load_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint geometryShader = 0;
    GLuint fragmentShader = R_GL_load_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    if (geometry_shader_source) {
        geometryShader = R_GL_load_shader(GL_GEOMETRY_SHADER, geometry_shader_source);
    }

    if (vertexShader == 0 || fragmentShader == 0 ||
        (geometry_shader_source && geometryShader == 0)) {
        fprintf(stderr, "Shader compilation failed\n");
        return 0;
    }

    GLuint program = glCreateProgram();
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
        return 0;
    }

    glDeleteShader(vertexShader);
    if (geometry_shader_source) glDeleteShader(geometryShader);
    glDeleteShader(fragmentShader);

    return program;
}

// ===== GESTIONE SHEET =====

int R_GL_set_sheet(float width, float height, R_GL_Scale sc) {
    scale = (float)sc.num / sc.den;
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

void R_GL_draw_sheet(void) {
    glUseProgram(shader_program_sheet);
    
    GLint viewLoc = glGetUniformLocation(shader_program_sheet, "uView");
    GLint modelLoc = glGetUniformLocation(shader_program_sheet, "uModel");
    GLint colorLoc = glGetUniformLocation(shader_program_sheet, "uColor");

    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view_matrix);
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model_matrix);
    
    glBindVertexArray(VAO_sheet);
    
    // Fill
    float color[4] = {0.10f, 0.110f, 0.12f, 1.0f};
    if (colorLoc != -1) glUniform4fv(colorLoc, 1, color);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
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

void R_GL_toggle_color_thickness() {
    colorThikness = (colorThikness == 0) ? 1 : 0;
}

// ===== GESTIONE GEOMETRIE =====
size_t upload_VBO(Segment_list *segment_list) {
    if (!segment_list) {
        printf("Error: segment_list is NULL\n");
        return 0;
    }
    
    if (segment_list->size == 0) {
        printf("Warning: No vertices to upload\n");
        return 0;
    }
    
    printf("Number of vertices: %zu, capacity: %zu\n", 
           segment_list->size, segment_list->capacity);
    
    // Bind existing VAO (already created in R_GL_init)
    glBindVertexArray(VAO_geometry);
    
    // Bind existing VBO and upload data
    glBindBuffer(GL_ARRAY_BUFFER, VBO_geometry);
    glBufferData(GL_ARRAY_BUFFER, 
                 segment_list->size * sizeof(R_GL_Vertex), 
                 segment_list->vertex, 
                 GL_STATIC_DRAW);
    
    // Verify buffer upload
    GLint bufferSize = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    printf("VBO buffer size: %d bytes (expected: %zu bytes)\n", 
           bufferSize, segment_list->size * sizeof(R_GL_Vertex));
    
    // Setup vertex attributes (only if not already configured)
    // location 0: position (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, position));
    glEnableVertexAttribArray(0);
    
    // location 1: color (vec4)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, color));
    glEnableVertexAttribArray(1);
    
    // location 2: distanceFromStart (float)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, distanceFromStart));
    glEnableVertexAttribArray(2);
    
    // location 3: attributes (unsigned int) - packed thickness|pattern|layer
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(R_GL_Vertex),
                           (void*)offsetof(R_GL_Vertex, pk_thickness_pattern_layer));
    glEnableVertexAttribArray(3);
    
    // Check for OpenGL errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("OpenGL Error after VBO upload: %d\n", err);
    	glBindBuffer(GL_ARRAY_BUFFER, 0);
    	glBindVertexArray(0);
		return 0; 
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
	return segment_list->size; 
}

void R_GL_draw_geometry(size_t vertex_count) {
    glUseProgram(shader_program_segment);
    
    GLint viewLoc = glGetUniformLocation(shader_program_segment, "uView");
    GLint modelLoc = glGetUniformLocation(shader_program_segment, "uModel");
    GLint colorThicknessLoc = glGetUniformLocation(shader_program_segment, "uColorThickness");
    GLint scaleLoc = glGetUniformLocation(shader_program_segment, "uScale");
    GLint vpLoc = glGetUniformLocation(shader_program_segment, "uViewportSize");
    
    // Nuovi uniform per i filtri a maschera 32-bit
    GLint thicknessMaskLoc = glGetUniformLocation(shader_program_segment, "uThicknessMask");
    GLint patternMaskLoc = glGetUniformLocation(shader_program_segment, "uPatternMask");
    GLint layerMaskLoc = glGetUniformLocation(shader_program_segment, "uLayerMask");
    
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view_matrix);
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model_matrix);
    if (colorThicknessLoc != -1) glUniform1i(colorThicknessLoc, colorThikness);
    if (scaleLoc != -1) glUniform1f(scaleLoc, scale);
    if (vpLoc != -1) glUniform2f(vpLoc, R_GL_w, R_GL_h);
    
    // Imposta i filtri (esempio: mostra tutto di default)
    // Puoi modificare questi valori per filtrare dinamicamente
    unsigned int thicknessMask = 0xFFFFFFFF;  // tutti i thickness visibili (bit 0-31)
    unsigned int patternMask = 0xFFFFFFFF;    // tutti i pattern visibili
    unsigned int layerMask = 0xFFFFFFFF;      // tutti i layer visibili
    
    if (thicknessMaskLoc != -1) glUniform1ui(thicknessMaskLoc, thicknessMask);
    if (patternMaskLoc != -1) glUniform1ui(patternMaskLoc, patternMask);
    if (layerMaskLoc != -1) glUniform1ui(layerMaskLoc, layerMask);
    
    glBindVertexArray(VAO_geometry);
    glDrawArrays(GL_LINES, 0, vertex_count);
    glBindVertexArray(0);
}

// ===== GESTIONE VIEWPORT E TRASFORMAZIONI =====

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
    R_GL_w = w;
    R_GL_h = h;
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

// ===== GESTIONE ZOOM E PAN =====

void R_GL_handle_zoom(SDL_Window* window, int wheel_y, float mouseX, float mouseY) {
    R_GL_Point pixelP = {mouseX, mouseY};
    R_GL_Point modelP;
    R_GL_pixel_to_point(window, &pixelP, &modelP);
    
    float zoom = model_matrix[0];
    R_GL_Point pan = {model_matrix[12], model_matrix[13]};
    
    printf("zoom: %f\n", zoom);
    
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

// ===== RENDERING =====

void R_GL_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void R_GL_swap_buffers(SDL_Window* window) {
    SDL_GL_SwapWindow(window);
}
