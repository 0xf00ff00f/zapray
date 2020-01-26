#version 450 core

uniform sampler2D sprite_texture;

in vec2 tex_coord;

out vec4 frag_color;

void main(void)
{
    frag_color = texture(sprite_texture, tex_coord);
}
