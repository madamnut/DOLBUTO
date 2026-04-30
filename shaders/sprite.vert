#version 450

layout(push_constant) uniform SpritePush
{
    vec4 rect;
    vec4 uvRect;
    vec4 color;
} pushData;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 fragUv;

vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

vec2 uvs[6] = vec2[](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 0.0)
);

void main()
{
    if (pushData.rect.z < 0.0)
    {
        gl_Position = vec4(inPosition, 0.0, 1.0);
        fragUv = inUv;
    }
    else
    {
        vec2 position = pushData.rect.xy + positions[gl_VertexIndex] * pushData.rect.zw;
        gl_Position = vec4(position, 0.0, 1.0);
        fragUv = pushData.uvRect.xy + uvs[gl_VertexIndex] * pushData.uvRect.zw;
    }
}
