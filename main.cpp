#include "panic.h"

#include "shaderprogram.h"
#include "pixmap.h"
#include "texture.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>
#include <algorithm>
#include <tuple>
#include <memory>
#include <iostream>

using QuadVerts = std::array<glm::vec2, 4>;

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

class SpriteBatcher
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

SpriteBatcher::SpriteBatcher()
{
    initialize_gl_resources();
}

SpriteBatcher::~SpriteBatcher()
{
    release_gl_resources();
}

void SpriteBatcher::set_view_rectangle(float left, float right, float bottom, float top)
{
    projection_matrix_ = glm::ortho(left, right, bottom, top);
}

void SpriteBatcher::start_batch()
{
    quads_.clear();
}

void SpriteBatcher::add_sprite(const TextureTile *tile, const QuadVerts &verts, int depth)
{
    quads_.emplace_back(tile, verts, depth);
}

void SpriteBatcher::render_batch() const
{
    std::vector<const Quad *> sorted_quads;
    sorted_quads.resize(quads_.size());
    std::transform(quads_.begin(), quads_.end(), sorted_quads.begin(), [](const Quad &quad) {
        return &quad;
    });
    std::stable_sort(sorted_quads.begin(), sorted_quads.end(), [](const Quad *a, const Quad *b) {
        return std::tie(a->depth, a->tile->sheet) < std::tie(b->depth, b->tile->sheet);
    });

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    auto *data_start = reinterpret_cast<GLfloat *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    glBindVertexArray(vao_); // XXX do we need to bind the buffer after doing this?

    program_.bind();
    program_.set_uniform(program_.uniform_location("mvp"), projection_matrix_);
    program_.set_uniform(program_.uniform_location("sprite_texture"), 0);

    const TextureSheet *cur_texture_sheet = nullptr;
    auto *data = data_start;

    int draw_calls = 0;
    const auto do_render = [&cur_texture_sheet, &data_start, &data, &draw_calls] {
        const auto vertex_count = data - data_start;
        if (vertex_count)
        {
            cur_texture_sheet->texture->bind();
            glDrawArrays(GL_TRIANGLES, 0, vertex_count);
            ++draw_calls;
        }
    };

    for (const auto *quad_ptr : sorted_quads)
    {
        const auto vertex_count = data - data_start;
        if (quad_ptr->tile->sheet != cur_texture_sheet)
        {
            if (data != data_start)
            {
                glUnmapBuffer(GL_ARRAY_BUFFER);
                do_render();
                data_start = reinterpret_cast<GLfloat *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
            }
            cur_texture_sheet = quad_ptr->tile->sheet;
            data = data_start;
        }

        const auto &verts = quad_ptr->verts;
        const auto &texture_coords = quad_ptr->tile->texture_coords;

        const auto emit_vertex = [&data, &verts, &texture_coords](int index) {
            *data++ = verts[index].x;
            *data++ = verts[index].y;
            *data++ = texture_coords[index].x;
            *data++ = texture_coords[index].y;
        };

        emit_vertex(0); emit_vertex(1); emit_vertex(2);
        emit_vertex(2); emit_vertex(3); emit_vertex(0);
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
    do_render();

    std::cout << "draw calls: " << draw_calls << '\n';

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

    glBindVertexArray(0);
}

void SpriteBatcher::release_gl_resources()
{
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

int main()
{
    constexpr auto window_width = 512;
    constexpr auto window_height = 512;

    if (!glfwInit())
        panic("glfwInit failed\n");

    glfwSetErrorCallback([](int error, const char *description) { panic("GLFW error: %s\n", description); });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 16);
    auto *window = glfwCreateWindow(window_width, window_height, "demo", nullptr, nullptr);
    if (!window)
        panic("glfwCreateWindow failed\n");

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewInit();

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mode) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);
    });

    glViewport(0, 0, window_width, window_height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        const auto initialize_sheet = [](TextureSheet &sheet, const char *image) {
            sheet.texture.reset(new Texture);

            auto pm = load_pixmap_from_png(image);
            sheet.texture->set_data(*pm);

            sheet.tiles.push_back({"test", {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}, &sheet});
        };

        TextureSheet sheet0;
        initialize_sheet(sheet0, "resources/images/gvim.png");

        TextureSheet sheet1;
        initialize_sheet(sheet1, "resources/images/firefox.png");

        const auto *tile0 = &sheet0.tiles.back();
        const auto *tile1 = &sheet1.tiles.back();

        SpriteBatcher batcher;
        batcher.set_view_rectangle(0, window_width, 0, window_height);

        while (!glfwWindowShouldClose(window))
        {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

            batcher.start_batch();
            batcher.add_sprite(tile0, {{{0, 0}, {0, 80}, {80, 80}, {80, 0}}}, 0);
            batcher.add_sprite(tile0, {{{40, 40}, {40, 120}, {120, 120}, {120, 40}}}, 1);
            batcher.add_sprite(tile1, {{{80, 80}, {80, 160}, {160, 160}, {160, 80}}}, 0);
            batcher.add_sprite(tile1, {{{120, 120}, {120, 200}, {200, 200}, {200, 120}}}, 1);
            batcher.render_batch();

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
