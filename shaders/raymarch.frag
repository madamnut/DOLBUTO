#version 450

const uint SUBCHUNK_EMPTY = 1u;
const uint SUBCHUNK_UNIFORM = 2u;
const uint SUBCHUNK_DENSE = 4u;
const int CHUNK_SIZE = 16;
const int SUBCHUNK_SIZE = 16;
const int SUBCHUNKS_PER_CHUNK = 32;
const uint INVALID_PHYSICAL_SLOT = 0xFFFFFFFFu;
const float TRACE_EPSILON = 0.001;
const float TRACE_TIE_EPSILON = 0.00001;

struct Subchunk
{
    uint flags;
    uint cellOffset;
    uint uniformCell;
    uint reserved;
};

layout(std430, binding = 0) readonly buffer SubchunkBuffer
{
    Subchunk items[];
} subchunkData;

layout(std430, binding = 1) readonly buffer SlotMapBuffer
{
    uint slots[];
} slotMapData;

layout(std430, binding = 2) readonly buffer CellPage0
{
    uint cells[];
} cellPage0;

layout(std430, binding = 3) readonly buffer CellPage1
{
    uint cells[];
} cellPage1;

layout(std430, binding = 4) readonly buffer CellPage2
{
    uint cells[];
} cellPage2;

layout(std430, binding = 5) readonly buffer CellPage3
{
    uint cells[];
} cellPage3;

layout(std430, binding = 6) readonly buffer BlockDefinitionBuffer
{
    uint faceLayers[];
} blockDefinitions;

layout(binding = 7) uniform sampler2DArray blockTextures;

layout(push_constant) uniform RaymarchPush
{
    vec4 camera;
    vec4 right;
    vec4 up;
    vec4 forward;
    vec4 viewportWorld;
    vec4 worldOffset;
} pushData;

layout(location = 0) out vec4 outColor;

float safeDivide(float numerator, float denominator)
{
    return abs(denominator) < 0.00001 ? 100000000.0 : numerator / denominator;
}

bool rayBox(vec3 origin, vec3 direction, vec3 boxMin, vec3 boxMax, out float tEnter, out float tExit)
{
    vec3 invDirection = 1.0 / direction;
    vec3 t0 = (boxMin - origin) * invDirection;
    vec3 t1 = (boxMax - origin) * invDirection;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    tEnter = max(max(tMin.x, tMin.y), tMin.z);
    tExit = min(min(tMax.x, tMax.y), tMax.z);
    return tExit >= max(tEnter, 0.0);
}

uint blockFromCell(uint cell)
{
    return cell & 0xFFFFu;
}

int positiveModulo(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

uint physicalSlotForCell(ivec3 cell, int worldWidth, int worldMinX, int worldMinZ)
{
    int localX = cell.x - worldMinX;
    int localZ = cell.z - worldMinZ;
    int chunkDiameter = worldWidth / CHUNK_SIZE;
    int chunkX = localX / CHUNK_SIZE;
    int chunkZ = localZ / CHUNK_SIZE;
    return slotMapData.slots[uint(chunkZ * chunkDiameter + chunkX)];
}

uint subchunkIndex(ivec3 cell, int worldWidth, int worldMinX, int worldMinZ)
{
    uint physicalSlot = physicalSlotForCell(cell, worldWidth, worldMinX, worldMinZ);
    if (physicalSlot == INVALID_PHYSICAL_SLOT)
    {
        return INVALID_PHYSICAL_SLOT;
    }

    int subchunkY = cell.y / SUBCHUNK_SIZE;
    return physicalSlot * SUBCHUNKS_PER_CHUNK + uint(subchunkY);
}

uint cellFromPage(uint pageIndex, uint index)
{
    if (pageIndex == 0u)
    {
        return cellPage0.cells[index];
    }
    if (pageIndex == 1u)
    {
        return cellPage1.cells[index];
    }
    if (pageIndex == 2u)
    {
        return cellPage2.cells[index];
    }
    return cellPage3.cells[index];
}

uint denseBlockId(Subchunk subchunk, ivec3 cell, int worldMinX, int worldMinZ)
{
    int localX = positiveModulo(cell.x - worldMinX, CHUNK_SIZE);
    int localY = positiveModulo(cell.y, SUBCHUNK_SIZE);
    int localZ = positiveModulo(cell.z - worldMinZ, CHUNK_SIZE);
    uint index = subchunk.cellOffset + uint((localY * CHUNK_SIZE + localZ) * CHUNK_SIZE + localX);
    return blockFromCell(cellFromPage(subchunk.reserved, index));
}

uint blockIdAt(ivec3 cell, int worldWidth, int worldDepth, int worldMinX, int worldMinZ, int worldHeight)
{
    if (cell.x < worldMinX || cell.x >= worldMinX + worldWidth || cell.y < 0 || cell.y >= worldHeight || cell.z < worldMinZ || cell.z >= worldMinZ + worldDepth)
    {
        return 0u;
    }

    uint index = subchunkIndex(cell, worldWidth, worldMinX, worldMinZ);
    if (index == INVALID_PHYSICAL_SLOT)
    {
        return 0u;
    }

    Subchunk subchunk = subchunkData.items[index];
    if ((subchunk.flags & SUBCHUNK_EMPTY) != 0u)
    {
        return 0u;
    }
    if ((subchunk.flags & SUBCHUNK_UNIFORM) != 0u)
    {
        return blockFromCell(subchunk.uniformCell);
    }
    if ((subchunk.flags & SUBCHUNK_DENSE) != 0u)
    {
        return denseBlockId(subchunk, cell, worldMinX, worldMinZ);
    }
    return 0u;
}

bool solidAt(ivec3 cell, int worldWidth, int worldDepth, int worldMinX, int worldMinZ, int worldHeight)
{
    return blockIdAt(cell, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight) != 0u;
}

float cornerOcclusion(bool sideA, bool sideB, bool corner)
{
    if (sideA && sideB)
    {
        return 0.55;
    }

    float openAmount = 3.0 - float((sideA ? 1 : 0) + (sideB ? 1 : 0) + (corner ? 1 : 0));
    return 0.55 + (openAmount / 3.0) * 0.45;
}

ivec3 normalCell(vec3 normal)
{
    return ivec3(
        normal.x > 0.5 ? 1 : (normal.x < -0.5 ? -1 : 0),
        normal.y > 0.5 ? 1 : (normal.y < -0.5 ? -1 : 0),
        normal.z > 0.5 ? 1 : (normal.z < -0.5 ? -1 : 0)
    );
}

void faceAxes(vec3 normal, out ivec3 uAxis, out ivec3 vAxis)
{
    if (normal.y > 0.5)
    {
        uAxis = ivec3(-1, 0, 0);
        vAxis = ivec3(0, 0, 1);
    }
    else if (normal.y < -0.5)
    {
        uAxis = ivec3(1, 0, 0);
        vAxis = ivec3(0, 0, 1);
    }
    else if (abs(normal.x) > 0.5)
    {
        uAxis = ivec3(0, 0, -1);
        vAxis = ivec3(0, -1, 0);
    }
    else
    {
        uAxis = ivec3(-1, 0, 0);
        vAxis = ivec3(0, -1, 0);
    }
}

float vertexOcclusion(
    ivec3 baseCell,
    ivec3 uAxis,
    ivec3 vAxis,
    int uSign,
    int vSign,
    int worldWidth,
    int worldDepth,
    int worldMinX,
    int worldMinZ,
    int worldHeight)
{
    bool sideU = solidAt(baseCell + uAxis * uSign, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    bool sideV = solidAt(baseCell + vAxis * vSign, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    bool corner = solidAt(baseCell + uAxis * uSign + vAxis * vSign, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    return cornerOcclusion(sideU, sideV, corner);
}

float ambientOcclusion(
    ivec3 cell,
    vec3 normal,
    vec2 uv,
    int worldWidth,
    int worldDepth,
    int worldMinX,
    int worldMinZ,
    int worldHeight)
{
    ivec3 uAxis;
    ivec3 vAxis;
    faceAxes(normal, uAxis, vAxis);

    ivec3 baseCell = cell + normalCell(normal);
    float ao00 = vertexOcclusion(baseCell, uAxis, vAxis, -1, -1, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    float ao10 = vertexOcclusion(baseCell, uAxis, vAxis, 1, -1, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    float ao01 = vertexOcclusion(baseCell, uAxis, vAxis, -1, 1, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    float ao11 = vertexOcclusion(baseCell, uAxis, vAxis, 1, 1, worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    return mix(mix(ao00, ao10, uv.x), mix(ao01, ao11, uv.x), uv.y);
}

float traceNudge(float t)
{
    return max(TRACE_EPSILON, abs(t) * 0.000001);
}

ivec3 cellFromRay(vec3 origin, vec3 direction, float t)
{
    vec3 position = origin + direction * (t + traceNudge(t));
    return ivec3(floor(position.x + 0.5), floor(position.y), floor(position.z + 0.5));
}

vec3 nextBoundaryForCell(ivec3 cell, ivec3 stepDirection)
{
    return vec3(
        stepDirection.x > 0 ? float(cell.x) + 0.5 : float(cell.x) - 0.5,
        stepDirection.y > 0 ? float(cell.y) + 1.0 : float(cell.y),
        stepDirection.z > 0 ? float(cell.z) + 0.5 : float(cell.z) - 0.5
    );
}

vec3 tMaxForCell(vec3 origin, vec3 direction, ivec3 cell, ivec3 stepDirection)
{
    vec3 nextBoundary = nextBoundaryForCell(cell, stepDirection);
    return vec3(
        safeDivide(nextBoundary.x - origin.x, direction.x),
        safeDivide(nextBoundary.y - origin.y, direction.y),
        safeDivide(nextBoundary.z - origin.z, direction.z)
    );
}

vec2 surfaceUv(vec3 hitPosition, vec3 normal)
{
    if (normal.y > 0.5)
    {
        return vec2(-hitPosition.x + 0.5, hitPosition.z + 0.5);
    }
    if (normal.y < -0.5)
    {
        return vec2(hitPosition.x + 0.5, hitPosition.z + 0.5);
    }
    if (abs(normal.x) > 0.5)
    {
        return vec2(-hitPosition.z + 0.5, -hitPosition.y);
    }
    return vec2(-hitPosition.x + 0.5, -hitPosition.y);
}

uint faceIndex(vec3 normal)
{
    if (normal.x > 0.5)
    {
        return 0u;
    }
    if (normal.x < -0.5)
    {
        return 1u;
    }
    if (normal.y > 0.5)
    {
        return 2u;
    }
    if (normal.y < -0.5)
    {
        return 3u;
    }
    if (normal.z > 0.5)
    {
        return 4u;
    }
    return 5u;
}

float textureLayer(uint id, vec3 normal)
{
    return float(blockDefinitions.faceLayers[id * 6u + faceIndex(normal)]);
}

void stepVoxel(inout float t, inout ivec3 cell, inout vec3 tMax, vec3 tDelta, ivec3 stepDirection, inout vec3 normal)
{
    float nextT = min(min(tMax.x, tMax.y), tMax.z);
    bool stepX = tMax.x <= nextT + TRACE_TIE_EPSILON;
    bool stepY = tMax.y <= nextT + TRACE_TIE_EPSILON;
    bool stepZ = tMax.z <= nextT + TRACE_TIE_EPSILON;

    t = nextT;
    if (stepX)
    {
        tMax.x += tDelta.x;
        cell.x += stepDirection.x;
        normal = vec3(-float(stepDirection.x), 0.0, 0.0);
    }
    if (stepY)
    {
        tMax.y += tDelta.y;
        cell.y += stepDirection.y;
        normal = vec3(0.0, -float(stepDirection.y), 0.0);
    }
    if (stepZ)
    {
        tMax.z += tDelta.z;
        cell.z += stepDirection.z;
        normal = vec3(0.0, 0.0, -float(stepDirection.z));
    }
}

void skipSubchunk(
    vec3 origin,
    vec3 direction,
    ivec3 stepDirection,
    int worldMinX,
    int worldMinZ,
    inout float t,
    inout ivec3 cell,
    inout vec3 tMax,
    vec3 tDelta,
    inout vec3 normal)
{
    int localX = cell.x - worldMinX;
    int localZ = cell.z - worldMinZ;
    int chunkX = localX / CHUNK_SIZE;
    int chunkZ = localZ / CHUNK_SIZE;
    int subchunkY = cell.y / SUBCHUNK_SIZE;

    float boundaryX = stepDirection.x > 0
        ? float(worldMinX + (chunkX + 1) * CHUNK_SIZE) - 0.5
        : float(worldMinX + chunkX * CHUNK_SIZE) - 0.5;
    float boundaryY = stepDirection.y > 0
        ? float((subchunkY + 1) * SUBCHUNK_SIZE)
        : float(subchunkY * SUBCHUNK_SIZE);
    float boundaryZ = stepDirection.z > 0
        ? float(worldMinZ + (chunkZ + 1) * CHUNK_SIZE) - 0.5
        : float(worldMinZ + chunkZ * CHUNK_SIZE) - 0.5;

    vec3 subT = vec3(
        safeDivide(boundaryX - origin.x, direction.x),
        safeDivide(boundaryY - origin.y, direction.y),
        safeDivide(boundaryZ - origin.z, direction.z)
    );

    const float invalidT = 100000000.0;
    float currentT = t;
    float nextX = subT.x > currentT + TRACE_EPSILON ? subT.x : invalidT;
    float nextY = subT.y > currentT + TRACE_EPSILON ? subT.y : invalidT;
    float nextZ = subT.z > currentT + TRACE_EPSILON ? subT.z : invalidT;
    float nextT = min(min(nextX, nextY), nextZ);

    if (nextT >= invalidT)
    {
        stepVoxel(t, cell, tMax, tDelta, stepDirection, normal);
        return;
    }

    if (nextX <= nextY && nextX <= nextZ)
    {
        t = nextX;
        normal = vec3(-float(stepDirection.x), 0.0, 0.0);
    }
    else if (nextY <= nextZ)
    {
        t = nextY;
        normal = vec3(0.0, -float(stepDirection.y), 0.0);
    }
    else
    {
        t = nextZ;
        normal = vec3(0.0, 0.0, -float(stepDirection.z));
    }

    cell = cellFromRay(origin, direction, t);
    tMax = tMaxForCell(origin, direction, cell, stepDirection);
}

void main()
{
    vec3 origin = pushData.camera.xyz;
    float tanHalfFov = pushData.camera.w;
    vec3 right = pushData.right.xyz;
    float aspect = pushData.right.w;
    vec3 up = pushData.up.xyz;
    float nearPlane = pushData.up.w;
    vec3 forward = pushData.forward.xyz;
    float farPlane = pushData.forward.w;
    vec2 viewportSize = pushData.viewportWorld.xy;
    int worldWidth = int(pushData.viewportWorld.z + 0.5);
    int worldDepth = int(pushData.viewportWorld.w + 0.5);
    int worldMinX = int(pushData.worldOffset.x);
    int worldMinZ = int(pushData.worldOffset.y);
    int worldHeight = int(pushData.worldOffset.z + 0.5);

    vec2 screen = gl_FragCoord.xy / viewportSize;
    vec2 ndc = vec2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    vec3 direction = normalize(forward + right * ndc.x * tanHalfFov * aspect + up * ndc.y * tanHalfFov);

    vec3 boxMin = vec3(float(worldMinX) - 0.5, 0.0, float(worldMinZ) - 0.5);
    vec3 boxMax = vec3(float(worldMinX + worldWidth) - 0.5, float(worldHeight), float(worldMinZ + worldDepth) - 0.5);
    float tEnter = 0.0;
    float tExit = 0.0;
    if (!rayBox(origin, direction, boxMin, boxMax, tEnter, tExit))
    {
        discard;
    }

    float t = max(max(tEnter, nearPlane), 0.0);
    ivec3 stepDirection = ivec3(
        direction.x >= 0.0 ? 1 : -1,
        direction.y >= 0.0 ? 1 : -1,
        direction.z >= 0.0 ? 1 : -1
    );

    ivec3 cell = cellFromRay(origin, direction, t);
    vec3 tMax = tMaxForCell(origin, direction, cell, stepDirection);
    vec3 tDelta = vec3(
        safeDivide(1.0, abs(direction.x)),
        safeDivide(1.0, abs(direction.y)),
        safeDivide(1.0, abs(direction.z))
    );

    vec3 normal = vec3(0.0, 1.0, 0.0);
    bool hit = false;
    float hitT = t;
    uint hitBlockId = 0;
    ivec3 hitCell = cell;

    for (int i = 0; i < 2048; ++i)
    {
        if (t > tExit || t > farPlane)
        {
            break;
        }

        if (cell.x >= worldMinX && cell.x < worldMinX + worldWidth && cell.y >= 0 && cell.y < worldHeight && cell.z >= worldMinZ && cell.z < worldMinZ + worldDepth)
        {
            uint index = subchunkIndex(cell, worldWidth, worldMinX, worldMinZ);
            if (index == INVALID_PHYSICAL_SLOT)
            {
                break;
            }

            Subchunk subchunk = subchunkData.items[index];
            if ((subchunk.flags & SUBCHUNK_EMPTY) != 0u)
            {
                skipSubchunk(origin, direction, stepDirection, worldMinX, worldMinZ, t, cell, tMax, tDelta, normal);
                continue;
            }

            uint id = 0;
            if ((subchunk.flags & SUBCHUNK_UNIFORM) != 0u)
            {
                id = blockFromCell(subchunk.uniformCell);
            }
            else if ((subchunk.flags & SUBCHUNK_DENSE) != 0u)
            {
                id = denseBlockId(subchunk, cell, worldMinX, worldMinZ);
            }

            if (id != 0u)
            {
                hit = true;
                hitT = t;
                hitBlockId = id;
                hitCell = cell;
                break;
            }
        }

        stepVoxel(t, cell, tMax, tDelta, stepDirection, normal);
    }

    if (!hit)
    {
        discard;
    }

    vec3 hitPosition = origin + direction * hitT;
    vec2 uv = surfaceUv(hitPosition, normal);
    float ao = ambientOcclusion(hitCell, normal, fract(uv), worldWidth, worldDepth, worldMinX, worldMinZ, worldHeight);
    float lod = clamp(floor(log2(max(hitT, 1.0) / 64.0)), 0.0, 5.0);
    vec3 color = textureLod(blockTextures, vec3(uv, textureLayer(hitBlockId, normal)), lod).rgb * ao;

    float viewZ = max(dot(hitPosition - origin, forward), nearPlane);
    gl_FragDepth = clamp(farPlane / (farPlane - nearPlane) - (nearPlane * farPlane) / ((farPlane - nearPlane) * viewZ), 0.0, 1.0);
    outColor = vec4(color, 1.0);
}
