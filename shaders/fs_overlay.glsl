#version 330 core

in vec4 vColor;
in vec4 vClip;   // x0, y0, x1, y1 in top-left pixels
in vec2 vPos;

out vec4 FragColor;

void main() {
    if (vPos.x < vClip.x || vPos.y < vClip.y ||
        vPos.x > vClip.z || vPos.y > vClip.w) {
        discard;
    }
    FragColor = vColor;
}
