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
//======================================================================================================
/**
 * @brief Initialize the VBO array
 * 
 * @param vbo_array Pointer to the VBO array to initialize
 * @return 0 on success, -1 on allocation error
 */
int vbo_array_init(VBO_Array *vbo_array) {
    vbo_array->size = 0;
    vbo_array->capacity = 10;  // Initial capacity
    vbo_array->objects = (VBO_Object*)malloc(vbo_array->capacity * sizeof(VBO_Object));
    
    if (!vbo_array->objects) {
        printf("Error: Failed to allocate VBO array\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Add a new VBO object to the array
 * 
 * @param vbo_array Pointer to the VBO array
 * @param vbo_id OpenGL VBO identifier
 * @param vertex_count Number of vertices in the VBO
 * @return Index of the added object, or -1 on error
 */
int vbo_array_add(VBO_Array *vbo_array, GLuint id, size_t vertex_count) {
    // Check if we need to expand the array
    if (vbo_array->size >= vbo_array->capacity) {
        size_t new_capacity = vbo_array->capacity * 2;
        VBO_Object *new_objects = (VBO_Object*)realloc(vbo_array->objects, 
                                                        new_capacity * sizeof(VBO_Object));
        
        if (!new_objects) {
            printf("Error: Failed to expand VBO array\n");
            return -1;
        }
        
        vbo_array->objects = new_objects;
        vbo_array->capacity = new_capacity;
    }
    
    // Initialize the new object with default values
    size_t index = vbo_array->size;
    vbo_array->objects[index].vbo_id = id;
    vbo_array->objects[index].vertex_count = vertex_count;
    vbo_array->objects[index].visible = 1;  // Visible by default
    float transform_matrix[16] = {
    	1, 0, 0, 0,
    	0, 1, 0, 0,
    	0, 0, 1, 0,
    	0, 0, 0, 1
	};

    // Initialize transform to identity matrix
	for (int i = 0; i < 16; i++)
		vbo_array->objects[index].transform[i] = transform_matrix[i];
    
	vbo_array->size++;
    
    printf("Added VBO object at index %zu: VBO=%u, vertices=%zu\n", 
           index, id, vertex_count);
    
    return (int)index;
}

/**
 * @brief Set transformation using TRS parameters
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object
 * @param tx Translation X
 * @param ty Translation Y
 * @param rotation Rotation in radians
 * @param sx Scale X
 * @param sy Scale Y
 * @return 0 on success, -1 if index out of bounds
 */
int vbo_array_set_transform(VBO_Array *vbo_array, GLuint id, 
                            float tx, float ty, float rotation, float scale) {

	if (!vbo_array || id == 0) return 0;
	float cosr = cosf(rotation);
	float sinr = sinf(rotation);
	float transform_matrix[16] = {
    	scale*cosr,  scale*sinr,  0.0f, 0.0f,
   		-scale*sinr, scale*cosr,  0.0f, 0.0f,
    	0.0f,        0.0f,        1.0f, 0.0f,
    	scale*(cosr*tx - sinr*ty), scale*(sinr*tx + cosr*ty), 0.0f, 1.0f
	};

	for (size_t index = 0; index < vbo_array->size ;index++){
		if (vbo_array->objects[index].vbo_id == id){
			for (int i = 0; i < 16; ++i)
				vbo_array->objects[index].transform[i] = transform_matrix[i];
    		return 0;

		}
	}
 	printf("Error: VBO id:%u notfound ", id);
    return -1;
}

/**
 * @brief Set visibility for a VBO object
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object
 * @param visible Visibility flag (0 = hidden, 1 = visible)
 * @return 0 on success, -1 if index out of bounds
 */
int vbo_array_set_visible(VBO_Array *vbo_array, GLuint id, int visible) {
	if (!vbo_array || id == 0) return 0;
	for (size_t index = 0; index < vbo_array->size ;index++){
		if (vbo_array->objects[index].vbo_id == id){
    		vbo_array->objects[index].visible = visible;
    		return 0;
		}
	}
	printf("Error: VBO id:%u notfound ", id);
    return -1;
}


/**
 * @brief Get VBO object by index
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object
 * @return Pointer to VBO_Object, or NULL if index out of bounds
 */
VBO_Object* vbo_array_get(VBO_Array *vbo_array, GLuint id) {
	if (!vbo_array || id == 0) return NULL;
	for (size_t index = 0; index < vbo_array->size ;index++){
		if (vbo_array->objects[index].vbo_id == id){
			return &vbo_array->objects[index];
		}
	}
 	printf("Error: VBO id:%u notfound ", id);
    return NULL;
}


/**
 * @brief Remove a VBO object from the array
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object to remove
 * @return 0 on success, -1 if index out of bounds
 */
int vbo_array_remove(VBO_Array *vbo_array, GLuint id) {
	if (!vbo_array || id == 0) return 0;
	size_t index = 0;
	int found= 0;
	for (index = 0; index < vbo_array->size ;index++){
		if (vbo_array->objects[index].vbo_id == id){
    		// Delete the OpenGL VBO
    		glDeleteBuffers(1, &vbo_array->objects[index].vbo_id);	
			found = 1;
			break;
		}
	}
	if (!found){
 		printf("Error: VBO id:%u notfound ", id);
    	return -1;
	}
    // Shift remaining elements left
    for (size_t i = index; i < vbo_array->size - 1; i++) {
        vbo_array->objects[i] = vbo_array->objects[i + 1];
    }
    
    vbo_array->size--;
    return 0;
}

/**
 * @brief Free all resources used by the VBO array
 * 
 * @param vbo_array Pointer to the VBO array
 */
void vbo_array_free(VBO_Array *vbo_array) {
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
 * @brief Upload segment list to a new VBO and add to VBO array
 * 
 * @param vbo_array Pointer to the VBO array
 * @param segment_list Pointer to the segment list
 * @return Index of the created VBO object, or -1 on error
 */
int upload_VBO(VBO_Array *vbo_array, Segment_list *segment_list) {
    if (!segment_list || !vbo_array) {
        printf("Error: NULL pointer passed to upload_VBO\n");
        return -1;
    }
    
    if (segment_list->size == 0) {
        printf("Warning: No vertices to upload\n");
        return -1;
    }
    
    printf("Uploading %zu vertices to new VBO\n", segment_list->size);
    
    // Create a new VBO
    GLuint vbo_id;
    glGenBuffers(1, &vbo_id);
    
    if (vbo_id == 0) {
        printf("Error: Failed to generate VBO\n");
        return -1;
    }
    
    // Bind and upload data
    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBufferData(GL_ARRAY_BUFFER, 
                 segment_list->size * sizeof(R_GL_Vertex), 
                 segment_list->vertex, 
                 GL_STATIC_DRAW);
    
    // Verify upload
    GLint bufferSize = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    printf("VBO %u buffer size: %d bytes (expected: %zu bytes)\n", 
           vbo_id, bufferSize, segment_list->size * sizeof(R_GL_Vertex));
    
    // Check for errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || bufferSize == 0) {
        printf("OpenGL Error after VBO upload: %d\n", err);
        glDeleteBuffers(1, &vbo_id);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return -1;
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Add to VBO array
    int index = vbo_array_add(vbo_array, vbo_id, segment_list->size);
    
    return index;
}

/**
 * @brief Draw a specific VBO object with its transformation matrix
 * 
 * @param vbo_array Pointer to the VBO array
 * @param index Index of the object to draw
 */
void R_GL_draw_vbo_object(VBO_Array *vbo_array, size_t index) {
    if (index >= vbo_array->size) {
        printf("Error: VBO index %zu out of bounds\n", index);
        return;
    }
    
    VBO_Object *obj = &vbo_array->objects[index];
    
    // Skip if not visible
    if (!obj->visible) {
        return;
    }
    
    glUseProgram(shader_program_segment);
    
    GLint viewLoc = glGetUniformLocation(shader_program_segment, "uView");
    GLint modelLoc = glGetUniformLocation(shader_program_segment, "uModel");
    GLint relativeLoc = glGetUniformLocation(shader_program_segment, "uRelative");
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
    // Set uniforms
    
   	if (relativeLoc != -1) glUniformMatrix4fv(relativeLoc, 1, GL_FALSE, obj->transform);  // Use object's transform
    
    // Set filter masks
    
    unsigned int thicknessMask = 0xFFFFFFFF;
    unsigned int patternMask = 0xFFFFFFFF;
    unsigned int layerMask = 0xFFFFFFFF;
    
    if (thicknessMaskLoc != -1) glUniform1ui(thicknessMaskLoc, thicknessMask);
    if (patternMaskLoc != -1) glUniform1ui(patternMaskLoc, patternMask);
    if (layerMaskLoc != -1) glUniform1ui(layerMaskLoc, layerMask);
    
    // Bind VAO and VBO for this object
    glBindVertexArray(VAO_geometry);
    glBindBuffer(GL_ARRAY_BUFFER, obj->vbo_id);
    
    // Setup vertex attributes
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, position));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, color));
    glEnableVertexAttribArray(1);
    
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(R_GL_Vertex),
                          (void*)offsetof(R_GL_Vertex, distanceFromStart));
    glEnableVertexAttribArray(2);
    
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(R_GL_Vertex),
                           (void*)offsetof(R_GL_Vertex, pk_thickness_pattern_layer));
    glEnableVertexAttribArray(3);
    
    // Draw
    glDrawArrays(GL_LINES, 0, obj->vertex_count);
    
    glBindVertexArray(0);
}

/**
 * @brief Draw all visible VBO objects
 * 
 * @param vbo_array Pointer to the VBO array
 */
void R_GL_draw_all_vbo_objects(VBO_Array *vbo_array) {
    for (size_t i = 0; i < vbo_array->size; i++) {
        R_GL_draw_vbo_object(vbo_array, i);
    }
}
