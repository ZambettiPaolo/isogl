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
uniform int uColorThickness; // flag modalità colore

void main() {
    // Unpacking degli attributi compressi (senza suffisso u)
    uint thickness   = aAttributes & 31u;
    uint patternType = (aAttributes >> 5u) & 31u;
    uint layer       = (aAttributes >> 10u) & 31u;
    
    // Trasforma in view space (non clip space!)
    vec4 viewPos = uView * uModel * vec4(aPosition, 0.0, 1.0);
    vPosition = viewPos.xy;
    gl_Position = viewPos;
    // Calcolo colore in base a flag uColorThickness o spessore
    if (uColorThickness == 0) {
        if (thickness < 25u) 
            vColor = vec4(1.0, 0.0, 0.0, 0.5);
        else if (thickness > 40u) 
            vColor = vec4(1.0, 1.0, 0.0, 1.0);
        else 
            vColor = vec4(1.0, 1.0, 1.0, 1.0);
    } else {
        vColor = aColor;
    }
    
    // Passaggio varyings
    vDistance = aDistanceFromStart;
    vPatternType = patternType;
    vThickness = thickness;
    vLayer = layer;
}

