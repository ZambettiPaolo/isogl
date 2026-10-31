#version 330 core

// Pick fragment shader for the vector font.  The vertex stage is shared with
// the display text pipeline.

uniform uint uPickId;

out uint fragId;

void main()
{
    fragId = uPickId;
}
