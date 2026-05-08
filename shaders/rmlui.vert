#version 450

layout(push_constant) uniform UiPush
{
    vec4 viewportAndTranslation;
} pushData;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;

void main()
{
    vec2 pixelPosition = inPosition + pushData.viewportAndTranslation.zw;
    vec2 ndc = vec2(
        pixelPosition.x / pushData.viewportAndTranslation.x * 2.0 - 1.0,
        pixelPosition.y / pushData.viewportAndTranslation.y * 2.0 - 1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor = inColor;
    fragUv = inUv;
}
