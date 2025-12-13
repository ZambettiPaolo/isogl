#include <SDL2/SDL_video.h>
#include <stdio.h>
#include <stddef.h>
#include "include/glad/glad.h"
#include "include/KHR/khrplatform.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

float zoomPan[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
 };
 

float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
 };
        
float scale[16] = {
            0.5, 0, 0, 0,
            0, 0.5, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
 };


float orto_matrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, -1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

int resize_gl_wiewport(SDL_Window* window, float* orto_matrix){
    int larghezza;
    int altezza;
    float viewSize =1.0f;
    SDL_GetWindowSize(window, &larghezza, &altezza);
    glViewport(0, 0, larghezza, altezza);
    float left = 0.0f;
    float right = (float)larghezza;
    float bottom = 0.0f;
    float top =  (float)altezza;
    if (larghezza >= altezza) {
    float ratio = (float)larghezza / (float)altezza;
        left   = -ratio * viewSize;
        right  =  ratio * viewSize;
        bottom = -viewSize;
        top    =  viewSize;
    } else {
        float ratio = (float)altezza / (float)larghezza;
        left   = -viewSize;
        right  =  viewSize;
        bottom = -ratio * viewSize;
        top    =  ratio * viewSize;
    }

    orto_matrix[0]  = 2.0f / (right - left);
    orto_matrix[5]  = 2.0f / (top - bottom);
    orto_matrix[12] = -(right + left) / (right - left);
    orto_matrix[13] = -(top + bottom) / (top - bottom);
    return 0;
}

// Vertex Shader

const char* vertex_shader_source = 
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
"uniform mat4 uZoomPanoramic;\n"
"uniform mat4 uView;\n"
"uniform mat4 uModel;\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position =  uZoomPanoramic * uProjection * uView * uModel * vec4(aPosition, 1.0);\n"
"    vColor = aColor;\n"
"    vDistance = aDistanceFromStart;\n"
"    vPatternType = aPatternType;\n"
"    vSegmentLength = aSegmentLength;\n"
"}\n";

const char* fragment_shader_source = 
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

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    // Controlla errori di compilazione
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

// Funzione per creare programma shader
GLuint createShaderProgram() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    // Controlla errori di linking
    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        printf("ERROR: Shader linking failed\n%s\n", infoLog);
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return shaderProgram;
}

struct Vertex{
    float position[3];
    float color[4];
    int patternType;
    float segmentLength;
    float distanceFromStart;


};

struct Vertex rettangolo[4] = 
{
    {
        {-0.5f, 0.5f, 0.0f},
        {0.0f, 0.5f, 0.5f, 1.0f},
        2,
        1.0f,
        0.0f

    },
    {
        {0.5f, 0.5f, 0.0f},
        {0.5f, 0.0f, 0.5f, 1.0f},
        2,
        1.0f,
        1.0f
    },
    {
        {0.5f, -0.5f, 0.0f},
        {0.5f, 0.5f, 0.0f, 1.0f},
        3,
        1.0f,
        0.0f
    },
    {
        {-0.5f, - 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        4,
        1.0f,
        0.0f

    }
};

unsigned int sequence[] = {
    0, 1, 
    1, 2,
    2, 3,
    3, 0,
};

int main (void){
    printf("start\n");
    if (SDL_Init(SDL_INIT_VIDEO) !=0){
        printf("SDL Init error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0){
        printf("SDL Get Current Display Mode: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window* window;
    window = SDL_CreateWindow(
        "Finestra massima",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        (dm.w/2),
        (dm.h/2),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
   
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if(gl_context == NULL){
        printf("Error GL context creation");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)){
        printf("Failed to initialaze GLAD\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("OpenGL Version:%s\n", glGetString(GL_VERSION));

// Crea shader program
    GLuint shaderProgram = createShaderProgram();
    
    // Crea VAO e VBO e EBO
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &EBO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rettangolo), rettangolo, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sequence), sequence, GL_STREAM_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,sizeof(struct Vertex), (void*)offsetof(struct Vertex, position));
    glEnableVertexAttribArray(0);
   
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,sizeof(struct Vertex),(void*)offsetof(struct Vertex, color));
    glEnableVertexAttribArray(1);
    
    glVertexAttribIPointer(2, 1, GL_INT, sizeof(struct Vertex), (void*)offsetof(struct Vertex, patternType));
    glEnableVertexAttribArray(2);
   
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,sizeof(struct Vertex), (void*)offsetof(struct Vertex, segmentLength));
    glEnableVertexAttribArray(3);
  
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,sizeof(struct Vertex), (void*)offsetof(struct Vertex, distanceFromStart));
    glEnableVertexAttribArray(4);
  
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);


    // Abilita linee antialiasing (opzionale)
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    
    // Imposta larghezza linea
    glLineWidth(2.0f);

    SDL_GL_SetSwapInterval(0);
    SDL_Event event;
    int quit = 0; 
    float dx = 0;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    int drag = 0;
    resize_gl_wiewport(window, orto_matrix);
    while (quit == 0){
        while (SDL_PollEvent(&event)){
        
            switch (event.type){
            case SDL_WINDOWEVENT:
                if (event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
                     resize_gl_wiewport(window, orto_matrix);
               }
                break;

            case SDL_MOUSEWHEEL:
                if (event.wheel.y>0){
                    zoom = zoom*2;
                    panX = panX*2 - event.wheel.mouseX;
                    panY = panY*2 - event.wheel.mouseY;
                }else if (event.wheel.y<0){
                    zoom = zoom/2;
                    panX = panX + event.wheel.mouseX/2.0f;
                    panY = panY + event.wheel.mouseY/2.0f;
                }
                zoomPan[0] = zoom;
                zoomPan[5] = zoom;
                zoomPan[12] = panX;
                zoomPan[13] = panY;
            break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button==SDL_BUTTON_LEFT) drag = 1;
            break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button==SDL_BUTTON_LEFT) drag = 0;
            break;

            case SDL_MOUSEMOTION:
            if (drag){
              panX = panX + event.motion.xrel;
              panY = panY + event.motion.yrel;
              zoomPan[12] = panX;
              zoomPan[13] = panY;
            }
            break;
            
            case SDL_QUIT:
                quit = 1;
                break;
            }
        }
        // Rendering
        
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Disegna la linea
        glUseProgram(shaderProgram);
        
        // Imposta matrici identità
        GLint projLoc = glGetUniformLocation(shaderProgram, "uProjection");
        GLint zoomPanoramic = glGetUniformLocation(shaderProgram, "uZoomPanoramic");
        GLint viewLoc = glGetUniformLocation(shaderProgram, "uView");
        GLint modelLoc = glGetUniformLocation(shaderProgram, "uModel");
        GLint patternScale = glGetUniformLocation(shaderProgram, "uPatternScale");

        scale[12] = dx;       
                   
        if (projLoc != -1)      glUniformMatrix4fv(projLoc,  1, GL_FALSE, orto_matrix);
        if (zoomPanoramic != -1)glUniformMatrix4fv(zoomPanoramic,  1, GL_FALSE, zoomPan);
        if (viewLoc != -1)      glUniformMatrix4fv(viewLoc,  1, GL_FALSE, identity);
        if (modelLoc != -1)     glUniformMatrix4fv(modelLoc, 1, GL_FALSE, scale);
        if (patternScale != -1) glUniform1f(patternScale, 1.0f );
       
        //glDrawElements(GL_LINES, 8, GL_UNSIGNED_INT, 0);
        glBindVertexArray(VAO);
        glDrawElements(GL_LINES, 8, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        SDL_GL_SwapWindow(window);
        SDL_Delay(20);
        dx = dx + 0.01;
        if( dx>1) dx = -1;
    }

    // Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

