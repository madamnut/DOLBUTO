#version 450

layout(std430, binding = 0) readonly buffer BlockBuffer
{
    uint ids[];
} blockData;

layout(binding = 1) uniform sampler2DArray blockTextures;

layout(push_constant) uniform RaymarchPush
{
    vec4 camera;
    vec4 right;
    vec4 up;
    vec4 forward;
    vec4 viewportWorld;
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

uint blockId(ivec3 cell, int worldWidth, int worldDepth)
{
    return blockData.ids[(cell.y * worldDepth + cell.z) * worldWidth + cell.x];
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

    vec2 screen = gl_FragCoord.xy / viewportSize;
    vec2 ndc = vec2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    vec3 direction = normalize(forward + right * ndc.x * tanHalfFov * aspect + up * ndc.y * tanHalfFov);

    vec3 boxMin = vec3(-0.5, 0.0, -0.5);
    vec3 boxMax = vec3(float(worldWidth) - 0.5, 512.0, float(worldDepth) - 0.5);
    float tEnter = 0.0;
    float tExit = 0.0;
    if (!rayBox(origin, direction, boxMin, boxMax, tEnter, tExit))
    {
        discard;
    }

    float t = max(max(tEnter, nearPlane), 0.0);
    vec3 position = origin + direction * t;
    ivec3 cell = ivec3(floor(position.x + 0.5), floor(position.y), floor(position.z + 0.5));
    ivec3 stepDirection = ivec3(
        direction.x >= 0.0 ? 1 : -1,
        direction.y >= 0.0 ? 1 : -1,
        direction.z >= 0.0 ? 1 : -1
    );

    vec3 nextBoundary = vec3(
        stepDirection.x > 0 ? float(cell.x) + 0.5 : float(cell.x) - 0.5,
        stepDirection.y > 0 ? float(cell.y) + 1.0 : float(cell.y),
        stepDirection.z > 0 ? float(cell.z) + 0.5 : float(cell.z) - 0.5
    );
    vec3 tMax = vec3(
        safeDivide(nextBoundary.x - origin.x, direction.x),
        safeDivide(nextBoundary.y - origin.y, direction.y),
        safeDivide(nextBoundary.z - origin.z, direction.z)
    );
    vec3 tDelta = vec3(
        safeDivide(1.0, abs(direction.x)),
        safeDivide(1.0, abs(direction.y)),
        safeDivide(1.0, abs(direction.z))
    );

    vec3 normal = vec3(0.0, 1.0, 0.0);
    bool hit = false;
    float hitT = t;
    uint hitBlockId = 0;

    for (int i = 0; i < 1024; ++i)
    {
        if (cell.x >= 0 && cell.x < worldWidth && cell.y >= 0 && cell.y < 512 && cell.z >= 0 && cell.z < worldDepth)
        {
            uint id = blockId(cell, worldWidth, worldDepth);
            if (id != 0)
            {
                hit = true;
                hitT = t;
                hitBlockId = id;
                break;
            }
        }

        if (t > tExit || t > farPlane)
        {
            break;
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z)
        {
            t = tMax.x;
            tMax.x += tDelta.x;
            cell.x += stepDirection.x;
            normal = vec3(-float(stepDirection.x), 0.0, 0.0);
        }
        else if (tMax.y <= tMax.z)
        {
            t = tMax.y;
            tMax.y += tDelta.y;
            cell.y += stepDirection.y;
            normal = vec3(0.0, -float(stepDirection.y), 0.0);
        }
        else
        {
            t = tMax.z;
            tMax.z += tDelta.z;
            cell.z += stepDirection.z;
            normal = vec3(0.0, 0.0, -float(stepDirection.z));
        }
    }

    if (!hit)
    {
        discard;
    }

    vec3 hitPosition = origin + direction * hitT;
    vec3 lightDirection = normalize(vec3(0.4, 0.9, 0.2));
    float light = 0.45 + max(dot(normal, lightDirection), 0.0) * 0.55;
    vec3 color = texture(blockTextures, vec3(surfaceUv(hitPosition, normal), textureLayer(hitBlockId, normal))).rgb * light;

    float viewZ = max(dot(hitPosition - origin, forward), nearPlane);
    gl_FragDepth = clamp(farPlane / (farPlane - nearPlane) - (nearPlane * farPlane) / ((farPlane - nearPlane) * viewZ), 0.0, 1.0);
    outColor = vec4(color, 1.0);
}
