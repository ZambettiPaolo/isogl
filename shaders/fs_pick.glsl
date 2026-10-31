#version 330 core

// Pick fragment shader: writes the opaque entity ID instead of a colour.
// The vertex/geometry stages are shared with the display pipeline, so the
// pixels tested are exactly the pixels the user sees.

uniform uint uPickId;

out uint fragId;

void main()
{
    fragId = uPickId;
}
