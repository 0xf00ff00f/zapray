#version 450 core

layout(location=0) in vec2 position;
layout(location=1) in vec2 vert_tex_coord;
layout(location=2) in vec4 vert_flat_color;

uniform mat4 mvp;

out vec2 tex_coord;
out vec4 flat_color;

void main(void)
{
    tex_coord = vert_tex_coord;
    flat_color = vert_flat_color;
    gl_Position = mvp * vec4(position, 0.0, 1.0);
}
