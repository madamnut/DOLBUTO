#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
} pushData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 fragUv;

void main()
{
    gl_Position = pushData.mvp * vec4(inPosition, 1.0);
    fragUv = inUv;
}
