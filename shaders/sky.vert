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

layout(location = 0) out vec2 fragNdc;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 position = positions[gl_VertexIndex];
    fragNdc = position;
    gl_Position = vec4(position, 0.0, 1.0);
}
