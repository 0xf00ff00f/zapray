#include "panic.h"

#include "shaderprogram.h"
#include "spritebatcher.h"
#include "texture.h"
#include "geometry.h"
#include "trajectory.h"
#include "level.h"
#include "tilesheet.h"
#include "dpadstate.h"
#include "world.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <cassert>
#include <array>
#include <vector>
#include <algorithm>
#include <tuple>
#include <memory>
#include <iostream>

#define DRAW_FRAMES

SpriteBatcher *g_sprite_batcher;
unsigned g_dpad_state = 0;

static constexpr const auto ViewportWidth = 400;
static constexpr const auto ViewportHeight = 600;
static constexpr const auto ViewportMargin = 12;
static constexpr const auto WindowWidth = 2 * ViewportWidth + 3 * ViewportMargin;
static constexpr const auto WindowHeight = ViewportHeight + 2 * ViewportMargin;

static constexpr const auto TicsPerSecond = 60;
static constexpr const auto MillisecondsPerTic = 1000.0f / TicsPerSecond;

class Game
{
public:
    Game();

    void advance(float dt);
    void render() const;

private:
    void advance_one_tic();

    int window_width_;
    int window_height_;
    std::unique_ptr<Level> level_;
    World local_;
    World remote_;
#ifdef DRAW_FRAMES
    ShaderProgram frame_program_;
    Geometry<std::tuple<glm::vec2>> frame_;
#endif
    float timestamp_ = 0.0f; // milliseconds
};

Game::Game()
    : level_(load_level("resources/levels/level-0.json"))
    , local_(ViewportWidth, ViewportHeight)
    , remote_(ViewportWidth, ViewportHeight)
{
    local_.initialize_level(level_.get());
    remote_.initialize_level(level_.get());

#ifdef DRAW_FRAMES
    constexpr float x0 = 0;
    constexpr float x1 = ViewportWidth;
    constexpr float y0 = 0;
    constexpr float y1 = ViewportHeight;

    static const std::vector<std::tuple<glm::vec2>> frame_verts =
        {{{x0, y0}}, {{x1, y0}}, {{x1, y1}}, {{x0, y1}}};
    frame_.set_data(frame_verts);

    frame_program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
    frame_program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
    frame_program_.link();
#endif
}

void Game::advance(float dt)
{
    timestamp_ += dt;
    while (timestamp_ > MillisecondsPerTic)
    {
        timestamp_ -= MillisecondsPerTic;
        advance_one_tic();
    }
}

void Game::advance_one_tic()
{
    local_.advance(g_dpad_state);
    remote_.advance(0);
}

void Game::render() const
{
    const auto project =
        glm::ortho(0.0f, static_cast<float>(WindowWidth), static_cast<float>(WindowHeight), 0.0f);

    glEnable(GL_SCISSOR_TEST);

    const auto draw_viewport = [this, &project](const World &world, int x_offset) {
        const auto translate
            = glm::translate(glm::mat4(1.0f), glm::vec3(x_offset, ViewportMargin, 0.0f));

        glScissor(x_offset, ViewportMargin, ViewportWidth, ViewportHeight);

        g_sprite_batcher->set_transform_matrix(project * translate);
        g_sprite_batcher->start_batch();
        world.render();
        g_sprite_batcher->render_batch();

#ifdef DRAW_FRAMES
        glDisable(GL_SCISSOR_TEST);

        const auto mvp = frame_program_.uniform_location("mvp");
        frame_program_.bind();
        frame_program_.set_uniform(mvp, project * translate);
        frame_.render(GL_LINE_LOOP);

        glEnable(GL_SCISSOR_TEST);
#endif
    };

    draw_viewport(local_, ViewportMargin);
    draw_viewport(remote_, 2 * ViewportMargin + ViewportWidth);

    glDisable(GL_SCISSOR_TEST);
}

static void update_dpad_state(GLFWwindow *window)
{
    unsigned state = 0;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        state |= DPad_Up;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        state |= DPad_Down;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        state |= DPad_Left;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        state |= DPad_Right;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        state |= DPad_Button;
    g_dpad_state = state;
}

int main()
{
    if (!glfwInit())
        panic("glfwInit failed\n");

    glfwSetErrorCallback([](int error, const char *description) { panic("GLFW error: %s\n", description); });

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 16);
    auto *window = glfwCreateWindow(WindowWidth, WindowHeight, "demo", nullptr, nullptr);
    if (!window)
        panic("glfwCreateWindow failed\n");

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewInit();

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mode) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);
    });

    glViewport(0, 0, WindowWidth, WindowHeight);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        cache_tilesheet("resources/tilesheets/sheet.json");
        g_sprite_batcher = new SpriteBatcher;

        {
            Game game;

            while (!glfwWindowShouldClose(window))
            {
                update_dpad_state(window);

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                game.advance(1000.f / 60.f);
                game.render();

                glfwSwapBuffers(window);
                glfwPollEvents();
            }
        }

        delete g_sprite_batcher;
        release_tilesheets();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
