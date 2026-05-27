#version 450

layout(push_constant) uniform CloudPush
{
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 sunDirection;
    vec4 dayDirection;
    vec4 cameraPosition;
    vec4 params;
    vec4 cloudParams;
} pushData;

layout(location = 0) in vec2 fragNdc;
layout(location = 0) out vec4 outColor;

float saturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

float screenNoise(vec2 pixel)
{
    vec3 value = fract(vec3(pixel.xyx) * 0.1031);
    value += dot(value, value.yzx + 33.33);
    return fract((value.x + value.y) * value.z);
}

float hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 local = fract(p);
    vec3 curve = local * local * (3.0 - 2.0 * local);

    float n000 = hash31(cell + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(cell + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, curve.x);
    float nx10 = mix(n010, n110, curve.x);
    float nx01 = mix(n001, n101, curve.x);
    float nx11 = mix(n011, n111, curve.x);
    float nxy0 = mix(nx00, nx10, curve.y);
    float nxy1 = mix(nx01, nx11, curve.y);
    return mix(nxy0, nxy1, curve.z);
}

bool cloudSlabIntersection(vec3 origin, vec3 direction, float baseY, float topY, out float entryT, out float exitT)
{
    if (abs(direction.y) < 0.0005)
    {
        if (origin.y < baseY || origin.y > topY)
        {
            return false;
        }
        entryT = 0.0;
        exitT = 420.0;
        return true;
    }

    float t0 = (baseY - origin.y) / direction.y;
    float t1 = (topY - origin.y) / direction.y;
    entryT = max(min(t0, t1), 0.0);
    exitT = max(t0, t1);
    return exitT > entryT;
}

float cloudDensityAt(vec3 worldPosition, float coverage, float cloudTime, float baseY, float topY)
{
    float height01 = saturate((worldPosition.y - baseY) / max(topY - baseY, 1.0));
    float heightMask = smoothstep(0.03, 0.25, height01) * (1.0 - smoothstep(0.78, 1.0, height01));

    vec2 wind = vec2(cloudTime * 900.0, cloudTime * 270.0);
    vec3 shapePosition = vec3((worldPosition.xz + wind) * 0.0045, worldPosition.y * 0.030);
    vec3 detailPosition = vec3((worldPosition.xz + wind * 1.7) * 0.018, worldPosition.y * 0.075);
    float shape = valueNoise(shapePosition);
    float detail = valueNoise(detailPosition);
    float cloudValue = saturate(shape * 0.82 + detail * 0.18);

    float threshold = mix(0.78, 0.36, coverage);
    float softness = mix(0.10, 0.18, coverage);
    float mask = smoothstep(threshold, threshold + softness, cloudValue);
    return mask * heightMask * mix(0.65, 1.25, coverage);
}

vec4 renderClouds(vec3 origin, vec3 direction, vec3 sunDirection, float sunVisibility, float noonFactor, float coverage)
{
    if (coverage <= 0.001)
    {
        return vec4(0.0);
    }

    float baseY = pushData.cloudParams.x;
    float topY = pushData.cloudParams.y;
    float cloudTime = pushData.cloudParams.z;
    float entryT = 0.0;
    float exitT = 0.0;
    if (!cloudSlabIntersection(origin, direction, baseY, topY, entryT, exitT))
    {
        return vec4(0.0);
    }

    const int StepCount = 18;
    float rayLength = min(exitT - entryT, 850.0);
    if (rayLength <= 0.0)
    {
        return vec4(0.0);
    }

    float stepLength = rayLength / float(StepCount);
    float jitter = screenNoise(gl_FragCoord.xy + vec2(cloudTime * 4096.0));
    float t = entryT + stepLength * jitter;
    vec3 accumulated = vec3(0.0);
    float alpha = 0.0;

    for (int i = 0; i < StepCount; ++i)
    {
        vec3 samplePosition = origin + direction * t;
        float density = cloudDensityAt(samplePosition, coverage, cloudTime, baseY, topY);
        if (density > 0.001)
        {
            float height01 = saturate((samplePosition.y - baseY) / max(topY - baseY, 1.0));
            float sampleAlpha = 1.0 - exp(-density * stepLength * 0.030);
            sampleAlpha *= 1.0 - alpha;

            float topLight = mix(0.58, 1.0, height01);
            float sunFacing = saturate(dot(direction, sunDirection) * 0.5 + 0.5);
            float light = mix(0.24, 0.92, sunVisibility) * topLight + sunFacing * sunVisibility * 0.18;
            vec3 dayCloud = mix(vec3(0.62, 0.65, 0.70), vec3(1.0, 0.98, 0.92), noonFactor);
            vec3 nightCloud = vec3(0.055, 0.065, 0.090);
            vec3 cloudColor = mix(nightCloud, dayCloud, sunVisibility) * light;

            accumulated += cloudColor * sampleAlpha;
            alpha += sampleAlpha;
            if (alpha > 0.985)
            {
                break;
            }
        }
        t += stepLength;
    }

    return vec4(accumulated / max(alpha, 0.001), saturate(alpha));
}

void main()
{
    float tanHalfFov = pushData.params.x;
    float aspect = pushData.params.y;
    float cloudCoverage = pushData.params.w;

    vec3 cloudRight = -pushData.cameraRight.xyz;
    vec3 cloudForward = normalize(vec3(pushData.cameraForward.x, -pushData.cameraForward.y, pushData.cameraForward.z));
    vec3 cloudUp = normalize(cross(cloudForward, cloudRight));
    vec3 cloudDirection = normalize(
        cloudForward +
        cloudRight * (fragNdc.x * tanHalfFov * aspect) -
        cloudUp * (fragNdc.y * tanHalfFov)
    );

    vec3 sunDirection = normalize(pushData.sunDirection.xyz);
    vec3 dayDirection = normalize(pushData.dayDirection.xyz);
    float sdotu = dot(dayDirection, vec3(0.0, 1.0, 0.0));
    float sunVisibility = smoothstep(-0.08, 0.08, sdotu);
    float noonFactor = smoothstep(0.18, 0.82, sdotu);

    vec4 clouds = renderClouds(pushData.cameraPosition.xyz, cloudDirection, sunDirection, sunVisibility, noonFactor, cloudCoverage);
    outColor = vec4(clouds.rgb, clouds.a);
}
