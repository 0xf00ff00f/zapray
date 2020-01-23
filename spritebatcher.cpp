#include "spritebatcher.h"

#include "texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>

SpriteBatcher::SpriteBatcher()
{
    initialize_gl_resources();
}

SpriteBatcher::~SpriteBatcher()
{
    release_gl_resources();
}

void SpriteBatcher::set_transform_matrix(const glm::mat4 &matrix)
{
    transform_matrix_ = matrix;
}

glm::mat4 SpriteBatcher::transform_matrix() const
{
    return transform_matrix_;
}

void SpriteBatcher::start_batch()
{
    quads_.clear();
}

void SpriteBatcher::add_sprite(const Tile *tile, const QuadVerts &verts, int depth)
{
    quads_.emplace_back(tile, verts, glm::vec4(0.0f), depth);
}

void SpriteBatcher::add_sprite(const Tile *tile, const QuadVerts &verts, const glm::vec4 &flat_color, int depth)
{
    quads_.emplace_back(tile, verts, flat_color, depth);
}

void SpriteBatcher::render_batch() const
{
    std::vector<const Quad *> sorted_quads;
    sorted_quads.resize(quads_.size());
    std::transform(quads_.begin(), quads_.end(), sorted_quads.begin(), [](const Quad &quad) {
        return &quad;
    });
    std::stable_sort(sorted_quads.begin(), sorted_quads.end(), [](const Quad *a, const Quad *b) {
        return std::tie(a->depth, a->tile->texture) < std::tie(b->depth, b->tile->texture);
    });

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    auto *data_start = reinterpret_cast<GLfloat *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    glBindVertexArray(vao_); // XXX do we need to bind the buffer after doing this?

    program_.bind();
    program_.set_uniform(program_.uniform_location("mvp"), transform_matrix_);
    program_.set_uniform(program_.uniform_location("sprite_texture"), 0);

    const Texture *cur_texture = nullptr;
    auto *data = data_start;

    int draw_calls = 0;
    const auto do_render = [&cur_texture, &data_start, &data, &draw_calls] {
        const auto vertex_count = (data - data_start) / 8;
        if (vertex_count)
        {
            cur_texture->bind();
            glDrawArrays(GL_TRIANGLES, 0, vertex_count);
            ++draw_calls;
        }
    };

    for (const auto *quad_ptr : sorted_quads)
    {
        const auto vertex_count = data - data_start;
        if (quad_ptr->tile->texture != cur_texture)
        {
            if (data != data_start)
            {
                glUnmapBuffer(GL_ARRAY_BUFFER);
                do_render();
                data_start = reinterpret_cast<GLfloat *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
            }
            cur_texture = quad_ptr->tile->texture;
            data = data_start;
        }

        const auto &verts = quad_ptr->verts;
        const auto &tex_coords = quad_ptr->tile->tex_coords;
        const auto &flat_color = quad_ptr->flat_color;

        const auto emit_vertex = [&data, &verts, &tex_coords, &flat_color](int index) {
            *data++ = verts[index].x;
            *data++ = verts[index].y;
            *data++ = tex_coords[index].x;
            *data++ = tex_coords[index].y;
            *data++ = flat_color.r;
            *data++ = flat_color.g;
            *data++ = flat_color.b;
            *data++ = flat_color.a;
        };

        emit_vertex(0); emit_vertex(1); emit_vertex(2);
        emit_vertex(2); emit_vertex(3); emit_vertex(0);
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
    do_render();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SpriteBatcher::initialize_gl_resources()
{
    program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/sprite.vert");
    program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/sprite.frag");
    program_.link();

    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLVertexSize * MaxQuadsPerBatch * 6, nullptr, GL_STATIC_DRAW);

    glBindVertexArray(vao_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, GLVertexSize, reinterpret_cast<GLvoid *>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, GLVertexSize, reinterpret_cast<GLvoid *>(2 * sizeof(GLfloat)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, GLVertexSize, reinterpret_cast<GLvoid *>(4 * sizeof(GLfloat)));

    glBindVertexArray(0);
}

void SpriteBatcher::release_gl_resources()
{
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

