#version 330 core

layout(lines) in;
layout(line_strip, max_vertices = 2) out;

// Varyings dal VS
in vec2 vPosition[];
in float vDistance[];
in vec4 vColor[];
flat in uint vPatternType[];
flat in uint vThickness[]; 
flat in uint vLayer[];

// Varyings verso FS
out float gDistance;
out vec4 gColor;
flat out uint gPatternType;
flat out uint gThickness;

// Uniforms
uniform float uScale;

// Filtri a maschera 32-bit
uniform uint uThicknessMask;
uniform uint uPatternMask;
uniform uint uLayerMask;

// Clipping box (view space)
const float xmin = -1.0;
const float xmax =  1.0;
const float ymin = -1.0;
const float ymax =  1.0;

// --- Liang-Barsky clipping ---
bool clipLine(inout vec2 p0, inout vec2 p1, out float t0, out float t1) {
    t0 = 0.0;
    t1 = 1.0;
    vec2 d = p1 - p0;
    
    // Sintassi corretta per GLSL 330
    float p[4];
    p[0] = -d.x;
    p[1] = d.x;
    p[2] = -d.y;
    p[3] = d.y;
    
    float q[4];
    q[0] = p0.x - xmin;
    q[1] = xmax - p0.x;
    q[2] = p0.y - ymin;
    q[3] = ymax - p0.y;
    
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
        } else {
            float r = q[i] / p[i];
            if (p[i] < 0.0) t0 = max(t0, r);
            else            t1 = min(t1, r);
            if (t0 > t1) return false;
        }
    }
    
    vec2 p0_new = mix(p0, p1, t0);
    vec2 p1_new = mix(p0, p1, t1);
    p0 = p0_new;
    p1 = p1_new;
    return true;
}

// --- Filtra segmenti usando bit-mask 32-bit ---
bool filterSegment(uint pattern, uint thickness, uint layer) {
    // Converte valore (0-31) in bit position
    uint thicknessBit = 1u << thickness;
    uint patternBit = 1u << pattern;
    uint layerBit = 1u << layer;
    
    // Verifica se il bit corrispondente è attivo nella maschera
    if ((thicknessBit & uThicknessMask) == 0u) return false;
    if ((patternBit & uPatternMask) == 0u) return false;
    if ((layerBit & uLayerMask) == 0u) return false;
    
    return true;
}

void main() {
    vec2 p0 = vPosition[0];
    vec2 p1 = vPosition[1];
    
   	// Filtro pattern/thickness/layer prima di emettere
	if (!filterSegment(vPatternType[0], vThickness[0], vLayer[0])) 
    	return;
    
    // Clipping nel view space
    float t0, t1;
    if (!clipLine(p0, p1, t0, t1)) 
        return;
    
    // Interpolazione distanze
    float dist0 = mix(vDistance[0], vDistance[1], t0);
    float dist1 = mix(vDistance[0], vDistance[1], t1);
    
    // Output vertices
    gColor = vColor[0];
    gPatternType = vPatternType[0];
    gThickness = vThickness[0];
    //gLayer = vLayer[0];
    
    gDistance = dist0;
	gl_Position = vec4(p0, 0.0, 1.0);
    EmitVertex();

    gDistance = dist1;
	gl_Position = vec4(p1, 0.0, 1.0);
    EmitVertex();
    
    EndPrimitive();
}


