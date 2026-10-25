#version 330 core

// Varyings dal GS
in float gDistance;
in vec4 gColor;
flat in uint gPatternType;
flat in uint gThickness;

out vec4 FragColor;

uniform float uScale;                 // sheet scale: model units -> mm
uniform float uWeightMM[8];           // ISO line weights in mm
uniform float uPatternRatios[150];    // 15 patterns x 10 ratios (multiples of thickness)
uniform int uPatternCount[15];        // 0 = continuous

const float MIN_DOT_RATIO = 0.5;      // zero-length ISO dots need a visible length
const int PATTERN_STRIDE = 10;

// Ratios alternate dash/gap starting with a dash.  A zero-length dash is an
// ISO dot and receives a minimum length so it survives rasterisation.
float ratioLength(int type, int i, float t) {
    float len = uPatternRatios[type * PATTERN_STRIDE + i] * t;
    if ((i & 1) == 0 && len <= 0.0) len = MIN_DOT_RATIO * t;
    return len;
}

bool applyPattern(uint patternType, float distance, float t) {
    int type = int(patternType);
    if (type < 0 || type >= 15) return true;

    int count = uPatternCount[type];
    if (count <= 0) return true;

    float cycle = 0.0;
    for (int i = 0; i < count; ++i) {
        cycle += ratioLength(type, i, t);
    }
    if (cycle <= 0.0) return true;

    float pos = mod(max(distance, 0.0), cycle);
    float acc = 0.0;
    for (int i = 0; i < count; ++i) {
        float len = ratioLength(type, i, t);
        if (pos < acc + len) {
            return (i & 1) == 0;
        }
        acc += len;
    }

    return true;
}

void main()
{
    uint weight = min(gThickness, 7u);
    float t = uWeightMM[weight];

    if (!applyPattern(gPatternType, gDistance * uScale, t)) {
        discard;
    }

    FragColor = gColor;
}
