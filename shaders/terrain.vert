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
    vec3 relativePosition = position - pushData.cameraPosition.xyz;
    vec2 uv = uvOrigin + uvEdgeU * useU + uvEdgeV * useV;
    uint aoIndex = (material >> (18u + corner * 2u)) & 0x3u;

    gl_Position = pushData.mvp * vec4(relativePosition, 1.0);
    fragUv = uv;
    fragAo = decodeAo(aoIndex);
    fragWorldPosition = relativePosition;
    fragTextureLayer = float(material & 0xFFu);
    fragMipDistanceScale = float((material >> 8u) & 0x3FFu) / 16.0;
    fragNormal = normalize(cross(edgeU, edgeV));
    fragAlphaBlend = float((material >> 26u) & 0x3Fu) / 63.0;
    fragSkyLight = float((packedLight >> 4u) & 0xFu) / 15.0;
    fragBlockLight = float(packedLight & 0xFu) / 15.0;
}
