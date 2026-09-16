#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aValue;

uniform mat4 uMVP;

out float vValue;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vValue = aValue;
}
