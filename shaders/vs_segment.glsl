#version 330 core

// Attributi del VBO (nuova struttura)
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aDistanceFromStart;
layout(location = 3) in uint aAttributes;  // packed: thickness|pattern|layer

// Varyings verso GS/FS
out vec2 vPosition;
out float vDistance;
out vec4 vColor;
flat out uint vPatternType;
flat out uint vThickness;
flat out uint vLayer;

// Uniforms
uniform mat4 uView;
uniform mat4 uModel;
uniform mat4 uRelative;
uniform int uColorThickness;            // 0 = colori-spessore, 1 = colori-stampa
uniform vec4 uWeightColor[8];           // predefined thickness -> colour map
uniform vec2 uPickScale;                // pick pass: 1,1 in normal rendering
uniform vec2 uPickOffset;               // pick pass: 0,0 in normal rendering

void main() {
    // Unpacking degli attributi compressi
    uint thickness   = aAttributes & 31u;
    uint patternType = (aAttributes >> 5u) & 31u;
    uint layer       = (aAttributes >> 10u) & 31u;

    // Trasforma in clip space (le matrici sono ortografiche, w = 1)
    vec4 viewPos = uView * uModel * uRelative * vec4(aPosition, 0.0, 1.0);
    viewPos.xy = viewPos.xy * uPickScale + uPickOffset;
    vPosition = viewPos.xy;
    gl_Position = viewPos;

    if (uColorThickness == 0) {
        vColor = uWeightColor[min(thickness, 7u)];
    } else {
        vColor = aColor;
    }

    vDistance = aDistanceFromStart;
    vPatternType = patternType;
    vThickness = thickness;
    vLayer = layer;
}
