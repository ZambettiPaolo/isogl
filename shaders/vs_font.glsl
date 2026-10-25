#version 330 core

layout(location = 0) in vec2 position;

uniform mat4 view_matrix;        // Aspect ratio
uniform mat4 model_matrix;       // Zoom
uniform mat4 relative_matrix;    // Blocco → Mondo
uniform mat4 string_matrix;      // Posizione/scala/rotazione stringa
void main() {
    gl_Position = view_matrix * model_matrix * 
                  relative_matrix * string_matrix * vec4(position, 0.0, 1.0);
}
