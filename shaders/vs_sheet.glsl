
#version 330 core
layout (location = 0) in vec2 aPos;
uniform mat4 uView;
uniform mat4 uModel;
void main(){
    gl_Position = uView * uModel * vec4(aPos, 0.0, 1.0);
}

