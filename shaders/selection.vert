#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = pushData.mvp * vec4(inPosition, 1.0);
}
