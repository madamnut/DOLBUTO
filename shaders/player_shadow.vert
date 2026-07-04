#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(set = 1, binding = 0, std430) readonly buffer PlayerNodeTransformBuffer
{
    mat4 nodeTransforms[];
} playerNodeTransformBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 5) in uint inNodeIndex;

void main()
{
    vec3 worldPosition = (playerNodeTransformBuffer.nodeTransforms[inNodeIndex] * vec4(inPosition, 1.0)).xyz;
    gl_Position = pushData.mvp * vec4(worldPosition - pushData.cameraPosition.xyz, 1.0);
}
