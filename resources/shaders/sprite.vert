#version 450 core

layout(location=0) in vec2 position;
layout(location=1) in vec2 vert_tex_coord;

uniform mat4 mvp;

out vec2 tex_coord;

void main(void)
{
    tex_coord = vert_tex_coord;
    gl_Position = mvp * vec4(position, 0.0, 1.0);
}
