#version 450

const uint SUBCHUNK_EMPTY = 1u;
const uint SUBCHUNK_UNIFORM = 2u;
const uint SUBCHUNK_DENSE = 4u;
const int CHUNK_SIZE = 16;
const int SUBCHUNK_SIZE = 16;
const int SUBCHUNKS_PER_CHUNK = 32;
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

layout(std430, binding = 1) readonly buffer CellBuffer
{
    uint cells[];
} cellData;

layout(binding = 2) uniform sampler2DArray blockTextures;

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

uint subchunkIndex(ivec3 cell, int worldWidth, int worldMinX, int worldMinZ)
{
    int localX = cell.x - worldMinX;
    int localZ = cell.z - worldMinZ;
    int chunkDiameter = worldWidth / CHUNK_SIZE;
    int chunkX = localX / CHUNK_SIZE;
    int chunkZ = localZ / CHUNK_SIZE;
    int subchunkY = cell.y / SUBCHUNK_SIZE;
    return uint(((chunkZ * chunkDiameter + chunkX) * SUBCHUNKS_PER_CHUNK) + subchunkY);
}

uint denseBlockId(Subchunk subchunk, ivec3 cell, int worldMinX, int worldMinZ)
{
    int localX = positiveModulo(cell.x - worldMinX, CHUNK_SIZE);
    int localY = positiveModulo(cell.y, SUBCHUNK_SIZE);
    int localZ = positiveModulo(cell.z - worldMinZ, CHUNK_SIZE);
    uint index = subchunk.cellOffset + uint((localY * CHUNK_SIZE + localZ) * CHUNK_SIZE + localX);
    return blockFromCell(cellData.cells[index]);
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

float textureLayer(uint id, vec3 normal)
{
    if (id == 2)
    {
        if (normal.y > 0.5)
        {
            return 1.0;
        }
        if (normal.y < -0.5)
        {
            return 2.0;
        }
        return 3.0;
    }
    if (id == 3)
    {
        return 4.0;
    }
    if (id == 4)
    {
        return 5.0;
    }
    if (id == 5)
    {
        return abs(normal.y) > 0.5 ? 7.0 : 6.0;
    }
    if (id == 6)
    {
        return 8.0;
    }
    if (id == 7)
    {
        return 9.0;
    }
    if (id == 8)
    {
        return abs(normal.y) > 0.5 ? 11.0 : 10.0;
    }
    return 0.0;
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

    if (subT.x <= subT.y && subT.x <= subT.z)
    {
        t = subT.x;
        normal = vec3(-float(stepDirection.x), 0.0, 0.0);
    }
    else if (subT.y <= subT.z)
    {
        t = subT.y;
        normal = vec3(0.0, -float(stepDirection.y), 0.0);
    }
    else
    {
        t = subT.z;
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

    for (int i = 0; i < 2048; ++i)
    {
        if (t > tExit || t > farPlane)
        {
            break;
        }

        if (cell.x >= worldMinX && cell.x < worldMinX + worldWidth && cell.y >= 0 && cell.y < worldHeight && cell.z >= worldMinZ && cell.z < worldMinZ + worldDepth)
        {
            Subchunk subchunk = subchunkData.items[subchunkIndex(cell, worldWidth, worldMinX, worldMinZ)];
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
    float lod = clamp(floor(log2(max(hitT, 1.0) / 64.0)), 0.0, 5.0);
    vec3 color = textureLod(blockTextures, vec3(surfaceUv(hitPosition, normal), textureLayer(hitBlockId, normal)), lod).rgb;

    float viewZ = max(dot(hitPosition - origin, forward), nearPlane);
    gl_FragDepth = clamp(farPlane / (farPlane - nearPlane) - (nearPlane * farPlane) / ((farPlane - nearPlane) * viewZ), 0.0, 1.0);
    outColor = vec4(color, 1.0);
}
