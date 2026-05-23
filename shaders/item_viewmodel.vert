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
layout(location = 3) in vec4 inCenterRotX;
layout(location = 4) in vec4 inRotYRotZLayerMip;
layout(location = 5) in vec2 inLight;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;
layout(location = 6) flat out float fragAlphaBlend;
layout(location = 7) flat out float fragSkyLight;
layout(location = 8) flat out float fragBlockLight;

const vec3 HeldItemBaseScale = vec3(0.34, 0.035, 0.34);

void main()
{
    vec3 value = inPosition * HeldItemBaseScale * max(inRotYRotZLayerMip.w, 0.001);

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

    vec3 viewPosition = inCenterRotX.xyz + value;
    gl_Position = pushData.mvp * vec4(viewPosition, 1.0);
    fragUv = inUv;
    fragAo = inAo;
    fragWorldPosition = viewPosition;
    fragTextureLayer = inRotYRotZLayerMip.z;
    fragMipDistanceScale = 0.0;
    fragAlphaBlend = 1.0;
    fragSkyLight = inLight.x;
    fragBlockLight = inLight.y;
}
