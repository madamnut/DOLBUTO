#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D ssaoTexture;
layout(binding = 2) uniform sampler2D sceneDepth;

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 params; // cameraPosition.xyz, worldTicks
    vec4 cloud;  // yaw, pitch, fovRadians, skyBrightness
} pushData;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec2 fragScreenUv;
layout(location = 0) out vec4 outColor;

const float TerrainNearPlane = 0.1;
const float TerrainFarPlane = 4000.0;
const float TicksPerDay = 28800.0;
const float TwoPi = 6.28318530718;
const float HalfPi = 1.57079632679;
const float CloudBaseY = 500.0;
const float CloudTopY = 700.0;
const int CloudSteps = 42;

float saturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

float linearViewDepth(float depth)
{
    return (TerrainNearPlane * TerrainFarPlane) / max(TerrainFarPlane - depth * (TerrainFarPlane - TerrainNearPlane), 0.0001);
}

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p)
{
    float value = 0.0;
    float amplitude = 0.56;
    float total = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        value += valueNoise(p) * amplitude;
        total += amplitude;
        p = p * 2.85 + vec3(17.0, 41.0, 11.0);
        amplitude *= 0.48;
    }
    return value / max(total, 0.0001);
}

vec3 sunDirectionFromTicks(float worldTicks)
{
    float phase = mod(worldTicks, TicksPerDay) / TicksPerDay;
    float skyAngle = HalfPi - phase * TwoPi;
    return normalize(vec3(cos(skyAngle), -sin(skyAngle), 0.0));
}

void cameraBasis(out vec3 terrainRight, out vec3 terrainUp, out vec3 terrainForward)
{
    float yaw = pushData.cloud.x;
    float pitch = pushData.cloud.y;
    float cosPitch = cos(pitch);
    vec3 forward = normalize(vec3(cosPitch * cos(yaw), sin(pitch), cosPitch * sin(yaw)));
    terrainForward = normalize(vec3(forward.x, -forward.y, forward.z));
    terrainRight = normalize(vec3(sin(yaw), 0.0, -cos(yaw)));
    terrainUp = normalize(cross(terrainForward, terrainRight));
}

vec3 viewRay(vec3 terrainRight, vec3 terrainUp, vec3 terrainForward)
{
    vec2 textureSizeValue = vec2(textureSize(sceneTexture, 0));
    float aspect = textureSizeValue.x / max(textureSizeValue.y, 1.0);
    float tanHalfFov = tan(pushData.cloud.z * 0.5);
    vec2 ndc = fragScreenUv * 2.0 - 1.0;
    return normalize(
        terrainForward +
        terrainRight * (ndc.x * tanHalfFov * aspect) +
        -terrainUp * (ndc.y * tanHalfFov));
}

float cloudDensity(vec3 worldPos)
{
    float h = saturate((worldPos.y - CloudBaseY) / (CloudTopY - CloudBaseY));
    float flatBase = smoothstep(0.015, 0.070, h);
    float softTop = 1.0 - smoothstep(0.72, 1.0, h);
    float bodyProfile = flatBase * softTop;
    float upperBillow = smoothstep(0.12, 0.50, h) * (1.0 - smoothstep(0.72, 1.0, h));

    float dayWind = pushData.params.w / TicksPerDay;
    vec3 p = worldPos;
    p.x += dayWind * 460.0;
    p.z -= dayWind * 1250.0;

    float weather = fbm(vec3(p.xz * 0.00110, 9.0));
    float largeShape = fbm(p * 0.00275 + vec3(2.0, 0.0, 17.0));
    float mediumBreakup = fbm(p * 0.00860 + vec3(13.0, 7.0, 29.0));
    float smallErosion = fbm(p * 0.02600 + vec3(53.0, 19.0, 5.0));

    float coverage = mix(0.48, 0.68, weather);
    float largeMask = smoothstep(coverage, coverage + 0.18, largeShape);
    float mediumBody = smoothstep(0.30, 0.82, mediumBreakup);
    float edgeMask = 1.0 - smoothstep(0.58, 0.92, largeMask);
    float topMask = smoothstep(0.34, 0.95, h);
    float erosionStrength = mix(0.10, 0.30, topMask) + edgeMask * 0.18;

    float core = largeMask * mix(0.68, 1.16, mediumBody);
    core += upperBillow * smoothstep(0.46, 0.82, mediumBreakup + largeShape * 0.25) * 0.18;
    core -= smallErosion * erosionStrength;
    core -= smoothstep(0.84, 1.0, h) * 0.42;

    float density = smoothstep(0.18, 0.78, core) * bodyProfile;
    return saturate(density);
}

vec4 traceClouds(vec3 cameraPosition, vec3 ray, vec3 terrainForward, float sceneDepthValue)
{
    if (abs(ray.y) < 0.0001)
    {
        return vec4(0.0);
    }

    float t0 = (CloudBaseY - cameraPosition.y) / ray.y;
    float t1 = (CloudTopY - cameraPosition.y) / ray.y;
    float tEnter = max(min(t0, t1), 0.0);
    float tExit = max(t0, t1);
    if (tExit <= tEnter)
    {
        return vec4(0.0);
    }

    float rayViewZ = max(dot(ray, terrainForward), 0.0001);
    if (sceneDepthValue < 0.9996)
    {
        float sceneViewDepth = linearViewDepth(sceneDepthValue);
        tExit = min(tExit, sceneViewDepth / rayViewZ - 0.35);
    }
    tExit = min(tExit, 2600.0);
    if (tExit <= tEnter)
    {
        return vec4(0.0);
    }

    float worldTicks = pushData.params.w;
    vec3 sunDirection = sunDirectionFromTicks(worldTicks);
    float daylight = smoothstep(-0.04, 0.20, sunDirection.y) * pushData.cloud.w;
    float twilight = smoothstep(-0.20, 0.08, sunDirection.y) * (1.0 - smoothstep(0.14, 0.45, sunDirection.y));
    vec3 dayLight = mix(vec3(1.12, 0.56, 0.28), vec3(1.04, 1.02, 0.95), smoothstep(0.10, 0.70, sunDirection.y));
    vec3 dayShadow = mix(vec3(0.48, 0.50, 0.62), vec3(0.56, 0.66, 0.80), smoothstep(0.10, 0.70, sunDirection.y));
    vec3 nightCloud = vec3(0.035, 0.045, 0.075);

    float stepLength = (tExit - tEnter) / float(CloudSteps);
    vec3 color = vec3(0.0);
    float alpha = 0.0;

    for (int i = 0; i < CloudSteps; ++i)
    {
        float t = tEnter + (float(i) + 0.5) * stepLength;
        vec3 pos = cameraPosition + ray * t;
        float density = cloudDensity(pos);
        if (density <= 0.001)
        {
            continue;
        }

        float h = saturate((pos.y - CloudBaseY) / (CloudTopY - CloudBaseY));
        float shadowDensity = cloudDensity(pos + sunDirection * 86.0);
        float lightVisibility = exp(-shadowDensity * 2.45);
        float powder = 1.0 - exp(-density * 5.0);
        float forwardScatter = pow(saturate(dot(ray, sunDirection)), 12.0) * daylight;
        float silver = pow(saturate(dot(ray, sunDirection)), 42.0) * daylight * (1.0 - lightVisibility) * 0.30;
        float heightLight = mix(0.72, 1.12, h);
        vec3 litColor = mix(dayShadow, dayLight, lightVisibility * heightLight);
        litColor += dayLight * forwardScatter * 0.18 + vec3(1.0, 0.88, 0.62) * silver;
        vec3 sampleColor = mix(nightCloud, litColor, saturate(daylight + twilight * 0.55));
        sampleColor *= mix(0.74, 1.08, powder);

        float distanceFade = 1.0 - smoothstep(1850.0, 2600.0, t);
        float sampleAlpha = (1.0 - exp(-density * stepLength * 0.018)) * distanceFade;
        sampleAlpha = min(sampleAlpha, 0.18);
        color += (1.0 - alpha) * sampleColor * sampleAlpha;
        alpha += (1.0 - alpha) * sampleAlpha;
        if (alpha > 0.94)
        {
            break;
        }
    }

    return vec4(color, saturate(alpha));
}

void main()
{
    vec4 sceneColor = texture(sceneTexture, fragUv);
    float ao = clamp(texture(ssaoTexture, fragUv).r, 0.78, 1.0);
    vec3 baseColor = sceneColor.rgb * ao;

    vec3 terrainRight;
    vec3 terrainUp;
    vec3 terrainForward;
    cameraBasis(terrainRight, terrainUp, terrainForward);
    vec3 ray = viewRay(terrainRight, terrainUp, terrainForward);
    vec3 cameraPosition = pushData.params.xyz;
    float depthValue = texture(sceneDepth, fragUv).r;
    vec4 clouds = traceClouds(cameraPosition, ray, terrainForward, depthValue);

    vec3 color = baseColor * (1.0 - clouds.a) + clouds.rgb;
    outColor = vec4(color, sceneColor.a);
}
