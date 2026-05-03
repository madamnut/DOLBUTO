#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
} pushData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAo;
layout(location = 3) in float inTextureLayer;
layout(location = 4) in float inMipDistanceScale;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;

void main()
{
    gl_Position = pushData.mvp * vec4(inPosition, 1.0);
    fragUv = inUv;
    fragAo = inAo;
    fragWorldPosition = inPosition;
    fragTextureLayer = inTextureLayer;
    fragMipDistanceScale = inMipDistanceScale;
}
