#version 450

layout(push_constant) uniform SkyPush
{
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 sunDirection;
    vec4 dayDirection;
    vec4 params;
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

vec3 mixSkyRamp(vec3 upColor, vec3 middleColor, vec3 downColor, float vdotu, float sunFacing)
{
    float upper = max(vdotu, 0.0);
    float middleMix = pow(1.0 - upper, mix(1.7, 1.2, sunFacing));
    vec3 color = mix(upColor, middleColor, middleMix);

    float lowerMix = smoothstep(0.0, 0.38, -vdotu + 0.03);
    return mix(color, downColor, lowerMix);
}

void main()
{
    float tanHalfFov = pushData.params.x;
    float aspect = pushData.params.y;

    vec3 viewDirection = normalize(
        pushData.cameraForward.xyz +
        -pushData.cameraRight.xyz * (fragNdc.x * tanHalfFov * aspect) +
        pushData.cameraUp.xyz * (fragNdc.y * tanHalfFov)
    );
    vec3 sunDirection = normalize(pushData.sunDirection.xyz);
    vec3 dayDirection = normalize(pushData.dayDirection.xyz);
    vec3 worldUp = vec3(0.0, 1.0, 0.0);

    float sdotu = dot(dayDirection, worldUp);
    float vdotu = dot(viewDirection, worldUp);
    float vdots = dot(viewDirection, sunDirection);

    float sunVisibility = smoothstep(-0.08, 0.08, sdotu);
    float noonFactor = smoothstep(0.18, 0.82, sdotu);
    float twilightFactor = sunVisibility * (1.0 - noonFactor) * smoothstep(-0.22, 0.12, sdotu);
    float nightFactor = 1.0 - sunVisibility;
    float sunFacing = saturate(vdots * 0.5 + 0.5);

    vec3 noonUp = vec3(0.20, 0.43, 0.86);
    vec3 noonMiddle = vec3(0.42, 0.68, 0.96);
    vec3 noonDown = vec3(0.78, 0.91, 1.00);

    vec3 twilightUp = vec3(0.10, 0.14, 0.34);
    vec3 twilightMiddle = vec3(0.42, 0.28, 0.45);
    vec3 twilightDown = vec3(1.00, 0.50, 0.24);

    vec3 nightUp = vec3(0.0008, 0.0015, 0.0060);
    vec3 nightMiddle = vec3(0.0040, 0.0070, 0.0200);
    vec3 nightDown = vec3(0.0100, 0.0160, 0.0380);

    vec3 dayUp = mix(twilightUp, noonUp, noonFactor);
    vec3 dayMiddle = mix(twilightMiddle, noonMiddle, noonFactor);
    vec3 dayDown = mix(twilightDown, noonDown, noonFactor);

    vec3 upColor = mix(nightUp, dayUp, sunVisibility);
    vec3 middleColor = mix(nightMiddle, dayMiddle, sunVisibility);
    vec3 downColor = mix(nightDown, dayDown, sunVisibility);

    vec3 color = mixSkyRamp(upColor, middleColor, downColor, vdotu, sunFacing);

    float horizonBand = pow(saturate(1.0 - abs(vdotu) * 2.1), 2.4);
    float sunHorizonSide = smoothstep(-0.25, 0.75, vdots);
    vec3 sunsetGlowColor = vec3(1.00, 0.42, 0.16);
    color = mix(color, sunsetGlowColor, horizonBand * twilightFactor * sunHorizonSide * 0.72);

    float solarGlare = pow(saturate(vdots), mix(64.0, 18.0, twilightFactor));
    vec3 glareColor = mix(vec3(1.00, 0.72, 0.42), vec3(1.00, 0.88, 0.60), noonFactor);
    color += glareColor * solarGlare * sunVisibility * (0.35 + twilightFactor * 0.75);

    float oppositeHorizon = pow(saturate(1.0 - abs(vdotu) * 2.4), 3.0) * smoothstep(0.0, 0.35, -vdots);
    color = mix(color, color + vec3(0.12, 0.07, 0.18), oppositeHorizon * twilightFactor * 0.35);

    float groundFade = smoothstep(0.0, 0.42, -vdotu);
    color *= mix(1.0, 0.45 + 0.25 * sunVisibility, groundFade);

    float dither = screenNoise(gl_FragCoord.xy);
    color += (dither - 0.5) / 512.0;

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
