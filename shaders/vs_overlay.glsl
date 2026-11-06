#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aClip;   // x0, y0, x1, y1 in top-left pixels

uniform mat4 uView;                   // pixel ortho, top-left origin

out vec4 vColor;
out vec4 vClip;
out vec2 vPos;

void main() {
    vPos = aPos;
    vClip = aClip;
    vColor = aColor;
    gl_Position = uView * vec4(aPos, 0.0, 1.0);
}
