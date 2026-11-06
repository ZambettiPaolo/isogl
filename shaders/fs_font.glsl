#version 330 core

uniform vec4 uColor;

uniform int uSelActive;
uniform vec4 uSelRect;                // xmin, ymin, xmax, ymax in framebuffer pixels
uniform vec4 uSelColor;
uniform int uSelected;                // persistent selection flag for this string
uniform int uClipActive;              // per-string overlay clip
uniform vec4 uClipRect;               // framebuffer pixels, bottom-left origin

out vec4 FragColor;
void main()
{
    if (uClipActive != 0 &&
        (gl_FragCoord.x < uClipRect.x || gl_FragCoord.x > uClipRect.z ||
         gl_FragCoord.y < uClipRect.y || gl_FragCoord.y > uClipRect.w)) {
        discard;
    }

    bool in_rect = uSelActive != 0 &&
        gl_FragCoord.x >= uSelRect.x && gl_FragCoord.x <= uSelRect.z &&
        gl_FragCoord.y >= uSelRect.y && gl_FragCoord.y <= uSelRect.w;

    if (uSelected != 0 || in_rect) {
        FragColor = uSelColor;
        return;
    }
    FragColor = uColor;
}










