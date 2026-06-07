#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloom;

void main()
{
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
    outBloom = vec4(0.0, 0.0, 0.0, 0.0);
}
