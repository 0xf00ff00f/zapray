#include "font.h"

#include "texture.h"
#include "shaderprogram.h"
#include "geometry.h"

namespace
{
class FontRenderer
{
public:
    FontRenderer();

    void render_text(const glm::mat4 &mvp, float x, float y, const std::string_view text);

private:
    void initialize_gl_resources();

    static constexpr const int MaxVerts = 1024;

    Texture texture_;
    ShaderProgram program_;
    using Vertex = std::tuple<glm::vec2, glm::vec2>;
    Geometry<Vertex> geometry_;
};

FontRenderer::FontRenderer()
    : texture_("resources/images/font.png")
{
    initialize_gl_resources();
}

void FontRenderer::initialize_gl_resources()
{
    program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/font.vert");
    program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/font.frag");
    program_.link();

    geometry_.set_data(nullptr, MaxVerts);
}

void FontRenderer::render_text(const glm::mat4 &mvp, float x, float y, const std::string_view text)
{
    constexpr const auto CharSize = 16;

    constexpr const auto CharCols = 95;
    constexpr const auto CharRows = 8;

    constexpr const auto du = 1.f / CharCols;
    constexpr const auto dv = 1.f / CharRows;

    x -= 0.5f * CharSize * text.size();
    y -= 0.5f * CharSize;

    auto *vertex = geometry_.map_vertex_data();
    for (char ch : text)
    {
        const float u = (ch - ' ') * du;
        const float v = 0.0f;

        *vertex++ = {glm::vec2(x, y), glm::vec2(u, v)};
        *vertex++ = {glm::vec2(x + CharSize, y), glm::vec2(u + du, v)};
        *vertex++ = {glm::vec2(x + CharSize, y + CharSize), glm::vec2(u + du, v + dv)};

        *vertex++ = {glm::vec2(x + CharSize, y + CharSize), glm::vec2(u + du, v + dv)};
        *vertex++ = {glm::vec2(x, y + CharSize), glm::vec2(u, v + dv)};
        *vertex++ = {glm::vec2(x, y), glm::vec2(u, v)};

        x += CharSize;
    }
    geometry_.unmap_vertex_data();

    program_.bind();
    program_.set_uniform(program_.uniform_location("mvp"), mvp);
    program_.set_uniform(program_.uniform_location("sprite_texture"), 0);

    texture_.bind();
    geometry_.render(GL_TRIANGLES, text.size() * 6);
}
}

void render_text(const glm::mat4 &mvp, float x, float y, const std::string_view text)
{
    static FontRenderer renderer;
    renderer.render_text(mvp, x, y, text);
}
