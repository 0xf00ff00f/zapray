#pragma once

#include "shaderprogram.h"

#include "tilesheet.h"

#include <glm/vec2.hpp>
#include <GL/glew.h>

#include <boost/noncopyable.hpp>

#include <array>
#include <vector>
#include <memory>

class SpriteBatcher : private boost::noncopyable
{
public:
    SpriteBatcher();
    ~SpriteBatcher();

    void set_view_rectangle(float left, float right, float bottom, float top);

    void start_batch();
    void add_sprite(const Tile *tile, const QuadVerts &verts, int depth);
    void add_sprite(const Tile *tile, const QuadVerts &verts, const glm::vec4 &flat_color, int depth);
    void render_batch() const;

private:
    void initialize_gl_resources();
    void release_gl_resources();

    struct Quad
    {
        const Tile *tile;
        QuadVerts verts;
        glm::vec4 flat_color;
        int depth;

        Quad(const Tile *tile, const QuadVerts &verts, const glm::vec4 &flat_color, int depth)
            : tile(tile)
            , verts(verts)
            , flat_color(flat_color)
            , depth(depth)
        { }
    };

    static constexpr const int GLVertexSize = (2 + 2 + 4) * sizeof(GLfloat);
    static constexpr const int MaxQuadsPerBatch = 1024;

    std::vector<Quad> quads_;
    GLuint vao_;
    GLuint vbo_;
    ShaderProgram program_;
    glm::mat4 projection_matrix_;
};
