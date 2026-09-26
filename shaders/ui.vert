#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 colour;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 vertex_colour;
layout(location=1) out vec2 tex_coord;
layout(push_constant) uniform Transform { mat4 matrix; } transform;
void main() {
    gl_Position = transform.matrix * vec4(position, 0.0, 1.0);
    vertex_colour = colour;
    tex_coord = uv;
}
