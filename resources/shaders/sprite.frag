#version 450 core

uniform sampler2D sprite_texture;

in vec2 tex_coord;
in vec4 flat_color;

out vec4 frag_color;

void main(void)
{
    vec4 color = texture(sprite_texture, tex_coord);
    frag_color = vec4(mix(color.rgb, flat_color.rgb, flat_color.a), color.a);
}
