#include "panic.h"

#include "shaderprogram.h"
#include "pixmap.h"
#include "texture.h"
#include "geometry.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <array>
#include <vector>
#include <algorithm>
#include <tuple>
#include <memory>
#include <iostream>

using QuadVerts = std::array<glm::vec2, 4>;

struct PathSegment
{
    glm::vec2 eval(float t) const;
    std::array<glm::vec2, 4> control_points;
};

using Path = std::vector<PathSegment>;

class Trajectory
{
public:
    Trajectory(const Path &path);
    float length() const { return length_; }

    glm::vec2 point_at(float distance) const;

private:
    struct Vertex
    {
        float distance;
        glm::vec2 position;
    };
    std::vector<Vertex> verts_;
    float length_;
};

glm::vec2 PathSegment::eval(float t) const
{
    const auto c0 = (1.0f - t) * (1.0f - t) * (1.0f - t);
    const auto c1 = 3.0f * (1.0f - t) * (1.0f - t) * t;
    const auto c2 = 3.0f * (1.0f - t) * t * t;
    const auto c3 = t * t * t;
    return c0 * control_points[0] + c1 * control_points[1] + c2 * control_points[2] + c3 * control_points[3];
}

Trajectory::Trajectory(const Path &path)
    : length_(0.0f)
{
    constexpr const auto VertsPerSegment = 20;
    verts_.reserve(path.size() * VertsPerSegment);

    auto add_vertex = [this, first = true, prev_pos = glm::vec2()](const glm::vec2 &pos) mutable {
        if (!first)
            length_ += glm::distance(prev_pos, pos);
        verts_.push_back({length_, pos});
        prev_pos = pos;
        first = false;
    };

    for (const auto &segment : path)
    {
        for (int i = 0; i < VertsPerSegment; ++i)
        {
            const auto t = static_cast<float>(i) / VertsPerSegment;
            const auto pos = segment.eval(t);
            add_vertex(pos);
        }
    }

    if (!path.empty())
    {
        const auto &pos = path.back().control_points[3];
        add_vertex(pos);
    }

    for (size_t i = 0; i < verts_.size(); ++i)
    {
        const auto &v = verts_[i];
        std::cout << i << ": " << glm::to_string(v.position) << ' ' << v.distance << '\n';
    }
}

glm::vec2 Trajectory::point_at(float distance) const
{
    if (distance <= 0.0f)
        return verts_.front().position;
    if (distance >= length_)
        return verts_.back().position;
    auto it = std::lower_bound(verts_.begin(), verts_.end(), distance, [](const Vertex &vert, float distance) {
        return vert.distance < distance;
    });
    assert(it != verts_.begin() && it != verts_.end());

    std::cout << distance << ' ' << std::distance(verts_.begin(), it) << '\n';

    const auto &v1 = *it;
    const auto &v0 = *std::prev(it);

    assert(distance >= v0.distance);
    assert(distance < v1.distance);
    const float t = (distance - v0.distance) / (v1.distance - v0.distance);

    return v0.position + t * (v1.position - v0.position);
}

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

            sheet.tiles.push_back({"test", {{{0, 1}, {0, 0}, {1, 0}, {1, 1}}}, &sheet});
        };

        TextureSheet sheet0;
        initialize_sheet(sheet0, "resources/images/gvim.png");

        TextureSheet sheet1;
        initialize_sheet(sheet1, "resources/images/firefox.png");

        const auto *tile0 = &sheet0.tiles.back();
        const auto *tile1 = &sheet1.tiles.back();

        SpriteBatcher batcher;
        batcher.set_view_rectangle(0, window_width, 0, window_height);

        PathSegment segment{{{{-20, 160}, {440, 120}, {360, 300}, {-20, 440}}}}; // omg so many braces

        std::vector<std::tuple<glm::vec2>> verts;

        constexpr const auto NumVerts = 21;
        for (int i = 0; i < NumVerts; ++i)
        {
            const auto t = static_cast<float>(i) / (NumVerts - 1);
            const auto v = segment.eval(t);
            verts.emplace_back(v);
            std::cout << v.x << ' ' << v.y << '\n';
        }

        Geometry<std::tuple<glm::vec2>> g;
        g.set_data(verts);

        ShaderProgram program;
        program.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
        program.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
        program.link();

        const auto projection_matrix =
            glm::ortho(0.0f, static_cast<float>(window_width),
                       0.0f, static_cast<float>(window_height));

        program.bind();
        program.set_uniform(program.uniform_location("mvp"), projection_matrix);

        Trajectory trajectory({segment});

        float distance = 0.0f;

        while (!glfwWindowShouldClose(window))
        {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

            program.bind();
            g.render(GL_LINE_STRIP);

            const auto pos = trajectory.point_at(distance);
            const auto hsize = 40.f;

            batcher.start_batch();
            batcher.add_sprite(tile0, {
                    {pos + glm::vec2(-hsize, -hsize),
                     pos + glm::vec2(-hsize, hsize),
                     pos + glm::vec2(hsize, hsize),
                     pos + glm::vec2(hsize, -hsize)}}, 0);
            batcher.render_batch();

            glfwSwapBuffers(window);
            glfwPollEvents();

            distance += 1.0f;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
