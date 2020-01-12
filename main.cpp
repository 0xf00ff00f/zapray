#include "panic.h"

#include "shaderprogram.h"
#include "spritebatcher.h"
#include "pixmap.h"
#include "texture.h"
#include "geometry.h"
#include "trajectory.h"

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

#define DRAW_ACTIVE_TRAJECTORIES

TextureSheet *g_sprite_sheet;
SpriteBatcher *g_sprite_batcher;

struct Wave
{
    int start_tic;
    int spawn_interval;
    int spawn_count;
    float foe_speed;
    const Trajectory *trajectory;
};

struct Level
{
    std::vector<std::unique_ptr<Trajectory>> trajectories;
    std::vector<std::unique_ptr<Wave>> waves;
};

struct Foe
{
    Foe(const Wave *wave);

    float speed;
    const Trajectory *trajectory;
    glm::vec2 position;
    float trajectory_position;
};

Foe::Foe(const Wave *wave)
    : speed(wave->foe_speed)
    , trajectory(wave->trajectory)
    , position(trajectory->point_at(0.0f))
    , trajectory_position(0.0f)
{
}

constexpr const int TicsPerSecond = 60;

class World
{
public:
    World(int window_width, int window_height);

    void initialize_level(const Level *level);
    void advance(float dt);
    void render();

private:
    void advance_waves();
    void advance_foes();

    const Level *cur_level_ = nullptr;

    struct ActiveWave
    {
        ActiveWave(const Wave *wave);
        const Wave *wave;
#ifdef DRAW_ACTIVE_TRAJECTORIES
        Geometry<std::tuple<glm::vec2>> geometry;
#endif
    };

    std::vector<std::unique_ptr<ActiveWave>> active_waves_;
    std::vector<Foe> foes_;
    float timestamp_ = 0.0f; // milliseconds
    int cur_tic_ = 0;
#ifdef DRAW_ACTIVE_TRAJECTORIES
    ShaderProgram trajectory_program_;
#endif
};

// XXX shouldn't need to pass window_width/height here
World::World(int window_width, int window_height)
{
#ifdef DRAW_ACTIVE_TRAJECTORIES
    trajectory_program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
    trajectory_program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
    trajectory_program_.link();

    const auto projection_matrix = glm::ortho(0.0f, static_cast<float>(window_width), 0.0f, static_cast<float>(window_height));
    trajectory_program_.bind();
    trajectory_program_.set_uniform(trajectory_program_.uniform_location("mvp"), projection_matrix);
#endif
}

void World::initialize_level(const Level *level)
{
    cur_level_ = level;

    foes_.clear();
    active_waves_.clear();

    timestamp_ = 0.0f;
    int cur_tic_ = 0;

    advance_waves();
}

void World::advance(float dt)
{
    timestamp_ += dt;
    constexpr auto MillisecondsPerTic = 1000.0f / TicsPerSecond;

    while (timestamp_ > MillisecondsPerTic)
    {
        timestamp_ -= MillisecondsPerTic;
        ++cur_tic_;
        advance_foes();
        advance_waves();
    }
}

void World::render()
{
#ifdef DRAW_ACTIVE_TRAJECTORIES
    trajectory_program_.bind();
    for (const auto &wave : active_waves_)
    {
        wave->geometry.render(GL_LINE_STRIP);
    }
#endif

    g_sprite_batcher->start_batch();

    const auto *tile = &g_sprite_sheet->tiles.front(); // XXX should make tiles a vector of unique_ptr
    const auto hsize = 20.f;

    for (const auto &foe : foes_)
    {
        const auto pos = foe.position;
        g_sprite_batcher->add_sprite(tile, {
                {pos + glm::vec2(-hsize, -hsize),
                pos + glm::vec2(-hsize, hsize),
                pos + glm::vec2(hsize, hsize),
                pos + glm::vec2(hsize, -hsize)}},
                0);
    }

    g_sprite_batcher->render_batch();
}

World::ActiveWave::ActiveWave(const Wave *wave)
    : wave(wave)
{
#ifdef DRAW_ACTIVE_TRAJECTORIES
    const auto *trajectory = wave->trajectory;

    std::vector<std::tuple<glm::vec2>> verts;

    constexpr const auto NumVerts = 100;
    for (int i = 0; i < NumVerts; ++i)
    {
        const auto t = static_cast<float>(i) / (NumVerts - 1);
        const auto d = t * trajectory->length();
        const auto v = trajectory->point_at(d);
        verts.emplace_back(v);
    }

    geometry.set_data(verts);
#endif
}

void World::advance_waves()
{
    for (const auto &wave : cur_level_->waves)
    {
        if (wave->start_tic == cur_tic_)
        {
            active_waves_.emplace_back(new ActiveWave(wave.get()));
        }
    }

    auto it = active_waves_.begin();
    while (it != active_waves_.end())
    {
        const auto *wave = (*it)->wave;
        bool erase = false;
        const auto wave_tic = cur_tic_ - wave->start_tic;
        if (wave_tic % wave->spawn_interval == 0)
        {
            foes_.emplace_back(wave);
            erase = (wave_tic == wave->spawn_interval * (wave->spawn_count - 1));
        }
        if (erase)
        {
            it = active_waves_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void World::advance_foes()
{
    auto it = foes_.begin();
    while (it != foes_.end())
    {
        auto &foe = *it;
        foe.trajectory_position += foe.speed;
        if (foe.trajectory_position > foe.trajectory->length())
        {
            it = foes_.erase(it);
        }
        else
        {
            foe.position = foe.trajectory->point_at(foe.trajectory_position);
            ++it;
        }
    }
}

std::unique_ptr<Level> create_test_level()
{
    const PathSegment segment0{{{{-20, 160}, {440, 80}, {360, 300}, {-20, 440}}}}; // omg so many braces
    const Path path0 = {segment0};

    const PathSegment segment1{{{{420, 160}, {-40, 80}, {40, 300}, {420, 440}}}};
    const Path path1 = {segment1};

    const PathSegment segment2{{{{-20, -20}, {60, 480}, {120, 400}, {280, -20}}}};
    const Path path2 = {segment2};

    auto trajectory0 = std::make_unique<Trajectory>(path0);
    auto trajectory1 = std::make_unique<Trajectory>(path1);
    auto trajectory2 = std::make_unique<Trajectory>(path2);

    auto wave0 = std::make_unique<Wave>();
    wave0->start_tic = 0;
    wave0->spawn_interval = 50;
    wave0->spawn_count = 5;
    wave0->foe_speed = 1.5;
    wave0->trajectory = trajectory0.get();

    auto wave1 = std::make_unique<Wave>();
    wave1->start_tic = 350;
    wave1->spawn_interval = 50;
    wave1->spawn_count = 5;
    wave1->foe_speed = 1.5;
    wave1->trajectory = trajectory1.get();

    auto wave2 = std::make_unique<Wave>();
    wave2->start_tic = 600;
    wave2->spawn_interval = 50;
    wave2->spawn_count = 3;
    wave2->foe_speed = 1.5;
    wave2->trajectory = trajectory2.get();

    auto level = std::make_unique<Level>();

    level->trajectories.push_back(std::move(trajectory0));
    level->trajectories.push_back(std::move(trajectory1));
    level->trajectories.push_back(std::move(trajectory2));

    level->waves.push_back(std::move(wave0));
    level->waves.push_back(std::move(wave1));
    level->waves.push_back(std::move(wave2));

    return level;
}

int main()
{
    constexpr auto window_width = 400;
    constexpr auto window_height = 600;

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
        // sprite sheet
        {
            g_sprite_sheet = new TextureSheet;
            g_sprite_sheet->texture.reset(new Texture);
            auto pm = load_pixmap_from_png("resources/images/gvim.png");
            g_sprite_sheet->texture->set_data(*pm);
            g_sprite_sheet->tiles.push_back({"test", {{{0, 1}, {0, 0}, {1, 0}, {1, 1}}}, g_sprite_sheet});
        }

        // batcher
        {
            g_sprite_batcher = new SpriteBatcher;
            g_sprite_batcher->set_view_rectangle(0, window_width, 0, window_height);
        }

        {
            auto level = create_test_level();

            World world(window_width, window_height);
            world.initialize_level(level.get());

            while (!glfwWindowShouldClose(window))
            {
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                world.advance(1000.f / 60.f);
                world.render();

                glfwSwapBuffers(window);
                glfwPollEvents();
            }
        }

        delete g_sprite_batcher;
        delete g_sprite_sheet;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
