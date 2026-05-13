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

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;

const vec3 DroppedItemScale = vec3(0.68, 0.05, 0.68);

void main()
{
    vec3 value = inPosition * DroppedItemScale;

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

    vec3 worldPosition = inCenterRotX.xyz + value;
    vec3 relativePosition = worldPosition - pushData.cameraPosition.xyz;
    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = inUv;
    fragAo = inAo;
    fragWorldPosition = relativePosition;
    fragTextureLayer = inRotYRotZLayerMip.z;
    fragMipDistanceScale = inRotYRotZLayerMip.w;
}
