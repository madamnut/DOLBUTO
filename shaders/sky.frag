#version 450

layout(push_constant) uniform SkyPush
{
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 sunPositionDirection;
    vec4 dayDirection;
    vec4 params;
} pushData;

layout(location = 0) in vec2 fragNdc;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloom;

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

vec3 mixSkyRamp(vec3 upColor, vec3 middleColor, vec3 downColor, float vdotu, float sunFacing)
{
    float upper = max(vdotu, 0.0);
    float middleMix = pow(1.0 - upper, mix(1.85, 1.25, sunFacing));
    vec3 color = mix(upColor, middleColor, middleMix);

    float lowerMix = smoothstep(0.0, 0.42, -vdotu + 0.04);
    return mix(color, downColor, lowerMix);
}

void main()
{
    float tanHalfFov = pushData.params.x;
    float aspect = pushData.params.y;

    vec3 viewDirection = normalize(
        pushData.cameraForward.xyz +
        pushData.cameraRight.xyz * (fragNdc.x * tanHalfFov * aspect) +
        -pushData.cameraUp.xyz * (fragNdc.y * tanHalfFov)
    );
    vec3 sunPositionDirection = normalize(pushData.sunPositionDirection.xyz);
    vec3 dayDirection = normalize(pushData.dayDirection.xyz);
    vec3 worldUp = vec3(0.0, 1.0, 0.0);

    float sdotu = dot(dayDirection, worldUp);
    float vdotu = dot(viewDirection, worldUp);
    float vdots = dot(viewDirection, sunPositionDirection);

    float daylight = smoothstep(-0.04, 0.16, sdotu);
    float highSun = smoothstep(0.10, 0.74, sdotu);
    float twilightFactor = smoothstep(-0.22, 0.05, sdotu) * (1.0 - smoothstep(0.14, 0.45, sdotu));
    float nightFactor = 1.0 - daylight;
    float sunFacing = saturate(vdots * 0.5 + 0.5);

    vec3 noonUp = vec3(0.18, 0.40, 0.84);
    vec3 noonMiddle = vec3(0.40, 0.67, 0.98);
    vec3 noonDown = vec3(0.76, 0.90, 1.00);

    vec3 twilightUp = vec3(0.055, 0.080, 0.205);
    vec3 twilightMiddle = vec3(0.26, 0.20, 0.36);
    vec3 twilightDown = vec3(1.05, 0.45, 0.17);

    vec3 nightUp = vec3(0.0010, 0.0018, 0.0065);
    vec3 nightMiddle = vec3(0.0040, 0.0060, 0.0160);
    vec3 nightDown = vec3(0.0100, 0.0120, 0.0280);

    vec3 dayUp = mix(twilightUp, noonUp, highSun);
    vec3 dayMiddle = mix(twilightMiddle, noonMiddle, highSun);
    vec3 dayDown = mix(twilightDown, noonDown, highSun);

    vec3 twilightBlendUp = mix(nightUp, twilightUp, twilightFactor);
    vec3 twilightBlendMiddle = mix(nightMiddle, twilightMiddle, twilightFactor);
    vec3 twilightBlendDown = mix(nightDown, twilightDown, twilightFactor);

    vec3 upColor = mix(twilightBlendUp, dayUp, daylight * highSun);
    vec3 middleColor = mix(twilightBlendMiddle, dayMiddle, daylight * highSun);
    vec3 downColor = mix(twilightBlendDown, dayDown, daylight * highSun);

    vec3 color = mixSkyRamp(upColor, middleColor, downColor, vdotu, sunFacing);

    float horizonBand = pow(saturate(1.0 - abs(vdotu) * 2.05), 2.25);
    float sunHorizonSide = smoothstep(-0.25, 0.75, vdots);
    vec3 sunsetGlowColor = vec3(1.10, 0.35, 0.10);
    color += sunsetGlowColor * horizonBand * twilightFactor * sunHorizonSide * 0.42;

    float solarFacing = saturate(vdots);
    float solarTight = pow(solarFacing, mix(82.0, 28.0, twilightFactor));
    float solarWide = pow(solarFacing, mix(13.0, 8.0, twilightFactor));
    vec3 glareColor = mix(vec3(1.00, 0.55, 0.25), vec3(1.00, 0.86, 0.58), highSun);
    color += glareColor * daylight * (solarTight * (0.20 + twilightFactor * 0.35) + solarWide * 0.035);

    float oppositeHorizon = pow(saturate(1.0 - abs(vdotu) * 2.4), 3.0) * smoothstep(0.0, 0.35, -vdots);
    color += vec3(0.10, 0.055, 0.16) * oppositeHorizon * twilightFactor * 0.22;

    vec3 moonDirection = -sunPositionDirection;
    float vdotm = dot(viewDirection, moonDirection);
    float moonGlow = pow(saturate(vdotm), 28.0) * nightFactor;
    color += vec3(0.05, 0.065, 0.11) * moonGlow;

    float horizonHaze = pow(saturate(1.0 - abs(vdotu) * 1.45), 2.0);
    color += vec3(0.045, 0.052, 0.060) * horizonHaze * daylight * (0.18 + 0.12 * highSun);

    float groundFade = smoothstep(0.0, 0.42, -vdotu);
    color *= mix(1.0, 0.34 + 0.36 * daylight, groundFade);

    float dither = screenNoise(gl_FragCoord.xy);
    color += (dither - 0.5) / mix(420.0, 768.0, daylight);

    outColor = vec4(max(color, vec3(0.0)), 1.0);
    outBloom = vec4(0.0, 0.0, 0.0, 1.0);
}
