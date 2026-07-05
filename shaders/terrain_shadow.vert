#version 450

layout(push_constant) uniform TerrainPush
{
    mat4 mvp;
    vec4 cameraPosition;
    vec4 fluidWaterParams;
} pushData;

layout(set = 1, binding = 0, std430) readonly buffer TerrainQuadBuffer
{
    uint packedQuads[];
} terrainQuadBuffer;

layout(location = 0) out vec2 fragUv;
layout(location = 1) flat out float fragTextureLayer;

const float ShadowDistortionBias = 0.8666667;

vec4 distortShadowClip(vec4 clipPosition)
{
    vec2 ndc = clipPosition.xy / max(abs(clipPosition.w), 0.0001);
    float distanceFromCenter = length(ndc);
    float distortion = max(distanceFromCenter * ShadowDistortionBias + (1.0 - ShadowDistortionBias), 0.0001);
    clipPosition.xy /= distortion;
    return clipPosition;
}

int decodeSignedFixed(uint packedValue)
{
    int magnitude = int(packedValue >> 1u);
    return (packedValue & 1u) != 0u ? -magnitude : magnitude;
}

int lowI16(uint packedValue)
{
    return (int(packedValue << 16u) >> 16);
}

int highI16(uint packedValue)
{
    return int(packedValue) >> 16;
}

vec2 windWave(vec3 position)
{
    float time = pushData.cameraPosition.w;
    float phaseA = position.x * 0.17 + position.y * 0.11 + position.z * 0.23;
    float phaseB = position.x * 0.07 - position.z * 0.19 + position.y * 0.13;
    return vec2(
        sin(phaseA + time * 1.65) * 0.65 + sin(phaseB + time * 0.95) * 0.35,
        sin(phaseA * 0.73 + time * 1.25) * 0.55 + sin(phaseB * 1.31 + time * 1.85) * 0.45);
}

void applyWaving(inout vec3 position, vec2 uv, uint wavingType)
{
    if (wavingType == 1u)
    {
        vec2 wind = windWave(position);
        float weight = clamp(1.0 - uv.y, 0.0, 1.0);
        weight = weight * weight;
        position.x += wind.x * 0.060 * weight;
        position.z += wind.y * 0.060 * weight;
    }
    else if (wavingType == 2u)
    {
        vec2 wind = windWave(position);
        position.x += wind.x * 0.028;
        position.y += (wind.x + wind.y) * 0.004;
        position.z += wind.y * 0.028;
    }
}

void main()
{
    uint quadIndex = uint(gl_VertexIndex) / 6u;
    uint triangleVertex = uint(gl_VertexIndex) - quadIndex * 6u;
    uint corner = triangleVertex == 0u ? 0u :
        (triangleVertex == 1u ? 1u :
        (triangleVertex == 2u ? 2u :
        (triangleVertex == 3u ? 0u :
        (triangleVertex == 4u ? 2u : 3u))));
    float useU = (corner == 1u || corner == 2u) ? 1.0 : 0.0;
    float useV = (corner == 2u || corner == 3u) ? 1.0 : 0.0;

    uint base = quadIndex * 11u;
    uint p0x = terrainQuadBuffer.packedQuads[base + 0u];
    uint p0y = terrainQuadBuffer.packedQuads[base + 1u];
    uint p0z = terrainQuadBuffer.packedQuads[base + 2u];
    uint edgeUxy = terrainQuadBuffer.packedQuads[base + 3u];
    uint edgeUzVx = terrainQuadBuffer.packedQuads[base + 4u];
    uint edgeVyz = terrainQuadBuffer.packedQuads[base + 5u];
    uint uv0 = terrainQuadBuffer.packedQuads[base + 6u];
    uint uvU = terrainQuadBuffer.packedQuads[base + 7u];
    uint uvV = terrainQuadBuffer.packedQuads[base + 8u];
    uint material = terrainQuadBuffer.packedQuads[base + 9u];
    uint packedLight = terrainQuadBuffer.packedQuads[base + 10u];

    vec3 origin = vec3(
        float(decodeSignedFixed(p0x)) / 256.0,
        float(decodeSignedFixed(p0y)) / 256.0,
        float(decodeSignedFixed(p0z)) / 256.0);
    vec3 edgeU = vec3(
        float(lowI16(edgeUxy)) / 256.0,
        float(highI16(edgeUxy)) / 256.0,
        float(lowI16(edgeUzVx)) / 256.0);
    vec3 edgeV = vec3(
        float(highI16(edgeUzVx)) / 256.0,
        float(lowI16(edgeVyz)) / 256.0,
        float(highI16(edgeVyz)) / 256.0);
    vec2 uvOrigin = vec2(float(lowI16(uv0)) / 256.0, float(highI16(uv0)) / 256.0);
    vec2 uvEdgeU = vec2(float(lowI16(uvU)) / 256.0, float(highI16(uvU)) / 256.0);
    vec2 uvEdgeV = vec2(float(lowI16(uvV)) / 256.0, float(highI16(uvV)) / 256.0);

    vec3 position = origin + edgeU * useU + edgeV * useV;
    vec2 uv = uvOrigin + uvEdgeU * useU + uvEdgeV * useV;
    uint wavingType = (packedLight >> 8u) & 0x3u;
    applyWaving(position, uv, wavingType);

    gl_Position = distortShadowClip(pushData.mvp * vec4(position - pushData.cameraPosition.xyz, 1.0));
    fragUv = uv;
    fragTextureLayer = float(material & 0xFFu);
}
