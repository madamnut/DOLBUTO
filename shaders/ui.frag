#version 450
layout(set=0, binding=0) uniform sampler2D image;
layout(location=0) in vec4 vertex_colour;
layout(location=1) in vec2 tex_coord;
layout(location=0) out vec4 output_colour;
void main() { output_colour = vertex_colour * texture(image, tex_coord); }

