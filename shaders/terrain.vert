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
layout(location = 1) out float fragAo;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragTextureLayer;
layout(location = 4) flat out float fragMipDistanceScale;
layout(location = 5) flat out vec3 fragNormal;
layout(location = 6) flat out float fragAlphaBlend;
layout(location = 7) flat out float fragSkyLight;
layout(location = 8) flat out float fragBlockLight;
layout(location = 9) flat out float fragWaterTint;

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

float decodeAo(uint value)
{
    if (value == 0u)
    {
        return 0.55;
    }
    if (value == 1u)
    {
        return 0.68;
    }
    if (value == 2u)
    {
        return 0.82;
    }
    return 1.0;
}

vec3 windWave(vec3 position)
{
    float time = pushData.cameraPosition.w;
    vec2 windDirection = vec2(0.8219, 0.5696);
    vec2 crossWind = vec2(-windDirection.y, windDirection.x);
    float along = dot(position.xz, windDirection);
    float across = dot(position.xz, crossWind);
    float gust = sin(along * 0.105 + position.y * 0.18 + time * 1.05) * 0.55
        + sin(along * 0.038 - across * 0.071 + time * 0.43) * 0.45;
    float flutter = sin(position.x * 0.73 - position.z * 0.61 + position.y * 0.29 + time * 2.35) * 0.20;
    float vertical = sin(along * 0.19 + position.y * 0.37 + time * 1.35) * 0.35;
    vec2 horizontal = windDirection * (gust + flutter) + crossWind * sin(across * 0.16 + time * 0.72) * 0.20;
    return vec3(horizontal.x, vertical, horizontal.y);
}

float wavingDistanceFade(vec3 position)
{
    float distanceFromCamera = length(position - pushData.cameraPosition.xyz);
    return 1.0 - smoothstep(128.0, 224.0, distanceFromCamera);
}

void applyWaving(inout vec3 position, vec2 uv, uint wavingType)
{
    if (wavingType == 1u)
    {
        vec3 wind = windWave(vec3(position.x, position.y * 0.55, position.z));
        float weight = smoothstep(0.05, 1.0, clamp(1.0 - uv.y, 0.0, 1.0));
        weight = weight * weight * wavingDistanceFade(position);
        position.x += wind.x * 0.075 * weight;
        position.z += wind.z * 0.075 * weight;
    }
    else if (wavingType == 2u)
    {
        vec3 wind = windWave(vec3(position.x * 0.75, position.y * 0.38, position.z * 0.75));
        float weight = wavingDistanceFade(position);
        position.x += wind.x * 0.035 * weight;
        position.y += wind.y * 0.009 * weight;
        position.z += wind.z * 0.035 * weight;
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
    uint lightValue = packedLight & 0xFFu;
    uint waterTintValue = (packedLight >> 10u) & 0xFu;
    applyWaving(position, uv, wavingType);
    vec3 relativePosition = position - pushData.cameraPosition.xyz;
    uint aoIndex = (material >> (18u + corner * 2u)) & 0x3u;

    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = uv;
    fragAo = decodeAo(aoIndex);
    fragWorldPosition = relativePosition;
    fragTextureLayer = float(material & 0xFFu);
    fragMipDistanceScale = float((material >> 8u) & 0x3FFu) / 16.0;
    fragNormal = normalize(cross(edgeU, edgeV));
    fragAlphaBlend = float((material >> 26u) & 0x3Fu) / 63.0;
    fragSkyLight = float((lightValue >> 4u) & 0xFu) / 15.0;
    fragBlockLight = float(lightValue & 0xFu) / 15.0;
    fragWaterTint = float(waterTintValue) / 15.0;
}
