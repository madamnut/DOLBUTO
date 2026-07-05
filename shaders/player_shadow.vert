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

const float ShadowDistortionBias = 0.8666667;

vec4 distortShadowClip(vec4 clipPosition)
{
    vec2 ndc = clipPosition.xy / max(abs(clipPosition.w), 0.0001);
    float distanceFromCenter = length(ndc);
    float distortion = max(distanceFromCenter * ShadowDistortionBias + (1.0 - ShadowDistortionBias), 0.0001);
    clipPosition.xy /= distortion;
    return clipPosition;
}

void main()
{
    vec3 worldPosition = (playerNodeTransformBuffer.nodeTransforms[inNodeIndex] * vec4(inPosition, 1.0)).xyz;
    gl_Position = distortShadowClip(pushData.mvp * vec4(worldPosition - pushData.cameraPosition.xyz, 1.0));
}
