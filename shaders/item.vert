#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
    vec4 fluidWaterParams;
} pushData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAo;
layout(location = 3) in float inLocalTextureLayer;
layout(location = 4) in vec4 inCenterRotX;
layout(location = 5) in vec4 inRotYRotZLayerMip;
layout(location = 6) in vec3 inScale;
layout(location = 7) in vec2 inLight;
layout(location = 8) in float inUvMirrorX;
layout(location = 9) in float inGeometryMirrorX;
layout(location = 10) in vec3 inBasisX;
layout(location = 11) in vec3 inBasisY;
layout(location = 12) in vec3 inBasisZ;
layout(location = 13) in float inWaterTint;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;
layout(location = 6) flat out float fragAlphaBlend;
layout(location = 7) flat out float fragSkyLight;
layout(location = 8) flat out float fragBlockLight;
layout(location = 9) flat out float fragWaterTint;

void main()
{
    vec3 value = inPosition * inScale;
    if (inGeometryMirrorX > 0.5)
    {
        value.x = -value.x;
    }

    float cosX = cos(inCenterRotX.w);
    float sinX = sin(inCenterRotX.w);
    value = vec3(
        value.x,
        value.y * cosX - value.z * sinX,
        value.y * sinX + value.z * cosX);

    float cosZ = cos(inRotYRotZLayerMip.y);
    float sinZ = sin(inRotYRotZLayerMip.y);
    value = vec3(
        value.x * cosZ - value.y * sinZ,
        value.x * sinZ + value.y * cosZ,
        value.z);

    float cosY = cos(inRotYRotZLayerMip.x);
    float sinY = sin(inRotYRotZLayerMip.x);
    value = vec3(
        value.x * cosY - value.z * sinY,
        value.y,
        value.x * sinY + value.z * cosY);

    vec3 worldPosition = inCenterRotX.xyz + inBasisX * value.x + inBasisY * value.y + inBasisZ * value.z;
    vec3 relativePosition = worldPosition - pushData.cameraPosition.xyz;
    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = vec2(inUvMirrorX > 0.5 ? 1.0 - inUv.x : inUv.x, inUv.y);
    fragAo = inAo;
    fragWorldPosition = relativePosition;
    fragTextureLayer = inLocalTextureLayer >= 0.0 ? inLocalTextureLayer : inRotYRotZLayerMip.z;
    fragMipDistanceScale = inRotYRotZLayerMip.w;
    fragAlphaBlend = 1.0;
    fragSkyLight = inLight.x;
    fragBlockLight = inLight.y;
    fragWaterTint = inWaterTint;
}
