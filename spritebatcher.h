#pragma once

#include "shaderprogram.h"

#include <glm/vec2.hpp>
#include <GL/glew.h>

#include <boost/noncopyable.hpp>

#include <array>
#include <vector>
#include <memory>

using QuadVerts = std::array<glm::vec2, 4>;

class Texture;
struct TextureSheet;

struct TextureTile
{
    std::string name;
    QuadVerts texture_coords;
    const TextureSheet *sheet;
};

struct TextureSheet
{
    std::unique_ptr<Texture> texture;
    std::vector<TextureTile> tiles;
};

class SpriteBatcher : private boost::noncopyable
{
public:
    SpriteBatcher();
    ~SpriteBatcher();

    void set_view_rectangle(float left, float right, float bottom, float top);

    void start_batch();
    void add_sprite(const TextureTile *tile, const QuadVerts &verts, int depth);
    void render_batch() const;

private:
    void initialize_gl_resources();
    void release_gl_resources();

    struct Quad
    {
        const TextureTile *tile;
        QuadVerts verts;
        int depth;

        Quad(const TextureTile *tile, const QuadVerts &verts, int depth)
            : tile(tile)
            , verts(verts)
            , depth(depth)
        { }
    };

    static constexpr const int GLVertexSize = 2 * 2 * sizeof(GLfloat);
    static constexpr const int MaxQuadsPerBatch = 1024;

    std::vector<Quad> quads_;
    GLuint vao_;
    GLuint vbo_;
    ShaderProgram program_;
    glm::mat4 projection_matrix_;
};
