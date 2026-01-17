#version 330 core

// Varyings dal GS
in float gDistance;
in vec4 gColor;
flat in uint gPatternType;
flat in uint gThickness;

float thickness[9] = float[9](0.12, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0);
float t = thickness[gThickness]; 
// Output
out vec4 FragColor;

// Uniform per scala globale (zoom/pan)
uniform float uScale;

// Funzione helper per il tratteggio
bool applyPattern(uint patternType, float distance, float scale)
{
    float d = distance / scale; // scala la distanza
	float patternLength;
	float pos;
    if (patternType == 1u) { //LINE_TYPE_DASHED
        patternLength = 15.0 * t; // 12.0 + 3.0
        pos = mod(d, patternLength);
        if (pos > 12.0 * t) return false;
    }
    else if (patternType == 2u) { //LINE_TYPE_DASHED_SPACED
        patternLength = 18.0 * t; // 12.0 + 6.0
     	pos = mod(d, patternLength);
        if (pos > 12.0 * t) return false;
	}
    else if (patternType == 3u) { //LINE_TYPE_LONG_DASH_DOT
		patternLength = 28 * t; // 20.0 + 3.0 + 2.0 + 3.0
     	pos = mod(d, patternLength);
		if ((pos > 20.0 * t && pos < 23.0 * t) || (pos > 25.0 * t)) return false;
    }
    else if (patternType == 5u) {
        // Tipo 3: puntini
        patternLength = 6.0;
        pos = mod(d, patternLength);
        if (pos > 1.0) return false;
    }
    else if (patternType == 6u) {
        // Tipo 4: linee lunghe
        patternLength = 20.0;
        pos = mod(d, patternLength);
        if (pos > 15.0) return false;
    }

    // default: line piena
    return true;
}

void main()
{

// Applica il tratteggio
    if (!applyPattern(gPatternType, gDistance, uScale))
        discard;

    // Colore finale
    FragColor = gColor;
}
