#version 330 core

uniform vec4 uColor;

uniform int uSelActive;
uniform vec4 uSelRect;                // xmin, ymin, xmax, ymax in framebuffer pixels
uniform vec4 uSelColor;
uniform int uSelected;                // persistent selection flag for this string

out vec4 FragColor;
void main()
{
    bool in_rect = uSelActive != 0 &&
        gl_FragCoord.x >= uSelRect.x && gl_FragCoord.x <= uSelRect.z &&
        gl_FragCoord.y >= uSelRect.y && gl_FragCoord.y <= uSelRect.w;

    if (uSelected != 0 || in_rect) {
        FragColor = uSelColor;
        return;
    }
    FragColor = uColor;
}










