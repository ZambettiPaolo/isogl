#version 330 core

layout(location = 0) in vec2 position;

uniform mat4 view_matrix;        // Aspect ratio
uniform mat4 model_matrix;       // Zoom
uniform mat4 relative_matrix;    // Blocco → Mondo
uniform mat4 string_matrix;      // Posizione/scala/rotazione stringa
uniform vec2 uPickScale;         // 1,1 in normal rendering
uniform vec2 uPickOffset;        // 0,0 in normal rendering
void main() {
    vec4 clip = view_matrix * model_matrix *
                relative_matrix * string_matrix * vec4(position, 0.0, 1.0);
    clip.xy = clip.xy * uPickScale + uPickOffset;
    gl_Position = clip;
}
