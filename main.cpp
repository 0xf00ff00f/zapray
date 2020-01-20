#include "panic.h"

#include "shaderprogram.h"
#include "spritebatcher.h"
#include "pixmap.h"
#include "texture.h"
#include "geometry.h"
#include "trajectory.h"
#include "tilesheet.h"
#include "util.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <rapidjson/document.h>

#include <cassert>
#include <array>
#include <vector>
#include <algorithm>
#include <tuple>
#include <memory>
#include <iostream>

#define DRAW_ACTIVE_TRAJECTORIES
#define DRAW_COLLISIONS

std::unique_ptr<TileSheet> g_sprite_sheet;
SpriteBatcher *g_sprite_batcher;

static constexpr const auto SpriteScale = 2.0f;

static constexpr const auto MissileSpawnInterval = 16;
static constexpr const auto DamageFlashInterval = 36;

static void draw_tile(const Tile *tile, const glm::vec2 &pos, const glm::vec4 &flat_color, int depth)
{
    const auto half_width = 0.5f * tile->size.x * SpriteScale;
    const auto half_height = 0.5f * tile->size.y * SpriteScale;

    const auto p0 = pos + glm::vec2(-half_width, -half_height);
    const auto p1 = pos + glm::vec2(-half_width, half_height);
    const auto p2 = pos + glm::vec2(half_width, half_height);
    const auto p3 = pos + glm::vec2(half_width, -half_height);

    g_sprite_batcher->add_sprite(tile, {{p0, p1, p2, p3}}, flat_color, depth);
}

static void draw_tile(const Tile *tile, const glm::vec2 &pos, int depth)
{
    draw_tile(tile, pos, glm::vec4(0.0f), depth);
}

static glm::vec2 tile_top_left(const Tile *tile, const glm::vec2 &center)
{
    return center - 0.5f * SpriteScale * glm::vec2(tile->size);
}

enum
{
    DPad_Up     = 1,
    DPad_Down   = 2,
    DPad_Left   = 4,
    DPad_Right  = 8,
    DPad_Button = 16,
};
unsigned g_dpad_state = 0;

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

struct Player
{
    Player();

    std::vector<const Tile *> frames;
    glm::vec2 position;
    int cur_frame = 0;
    int fire_tics = 0;
};

Player::Player()
{
    const auto frame_tiles = {"player-0.png", "player-1.png", "player-2.png", "player-3.png"};
    for (const auto tile : frame_tiles)
    {
        frames.push_back(g_sprite_sheet->find_tile(tile));
    }
}

struct Foe
{
    Foe(const Wave *wave);

    float speed;
    const Trajectory *trajectory;
    glm::vec2 position;
    float trajectory_position;
    int damage_tics = 0;
};

Foe::Foe(const Wave *wave)
    : speed(wave->foe_speed)
    , trajectory(wave->trajectory)
    , position(trajectory->point_at(0.0f))
    , trajectory_position(0.0f)
{
}

struct Missile
{
    glm::vec2 position;
};

struct Sprite
{
public:
    Sprite(const Tile *tile);

    const Tile *tile;

    bool collides_with(const Sprite &other, const glm::vec2 &pos) const;

private:
    void initialize_mask();

    using Word = uint64_t;
    static constexpr const auto BitsPerWord = 8 * sizeof(Word);
    using Bitmask = std::vector<Word>;

    bool test_bitmasks(const Bitmask &mask0, const Bitmask &mask1, int offset) const;

    std::vector<Bitmask> masks_;
};

Sprite::Sprite(const Tile *tile)
    : tile(tile)
{
    initialize_mask();
}

bool Sprite::collides_with(const Sprite &other, const glm::vec2 &pos) const
{
    const auto cols = tile->size.x;
    const auto rows = tile->size.y;

    const auto other_cols = other.tile->size.x;
    const auto other_rows = other.tile->size.y;

    const auto row_offset = static_cast<int>(pos.y);
    const auto col_offset = static_cast<int>(pos.x);

    if (col_offset >= cols || col_offset < -other_cols)
        return false;

    if (row_offset >= rows || row_offset < -other_rows)
        return false;

    assert(masks_.size() == rows);

    const int start_row = std::max(0, row_offset);
    const int end_row = std::min(rows, row_offset + other_rows);

    for (int row = start_row; row < end_row; ++row)
    {
        const auto other_row = row - row_offset;
        assert(other_row >= 0 && other_row < other_rows);

        const auto &mask = masks_[row];
        const auto &other_mask = other.masks_[other_row];

        if (col_offset >= 0)
        {
            if (test_bitmasks(mask, other_mask, col_offset))
                return true;
        }
        else
        {
            if (test_bitmasks(other_mask, mask, -col_offset))
                return true;
        }
    }

    return false;
}

bool Sprite::test_bitmasks(const Bitmask &mask0, const Bitmask &mask1, int shift) const
{
    const auto word_offset = shift / BitsPerWord;
    const auto word_shift = shift % BitsPerWord;

    for (int i = 0, j = word_offset; i < mask1.size() && j < mask0.size(); ++i, ++j)
    {
        const auto w0 = mask1[i];

        auto w1 = mask0[j] << word_shift;
        if (j + 1 < mask0.size())
            w1 |= (mask0[j + 1] >> (BitsPerWord - word_shift));

        if (w0 & w1)
            return true;
    }

    return false;
}

void Sprite::initialize_mask()
{
    const auto *pm = tile->texture->pixmap();
    assert(pm->type == Pixmap::PixelType::RGBAlpha); // XXX for now

    const auto *pixels = reinterpret_cast<const uint32_t *>(pm->pixels.data());

    masks_.reserve(tile->size.y);

    for (int i = 0; i < tile->size.y; ++i)
    {
        const auto mask_words = (tile->size.x + BitsPerWord - 1) / BitsPerWord;

        Bitmask mask(mask_words, 0);

        for (int j = 0; j < tile->size.x; ++j)
        {
            const uint32_t pixel = pixels[(i + tile->position.y) * pm->width + j + tile->position.x];
            if ((pixel >> 24) > 0x7f)
            {
                mask[j / BitsPerWord] |= (1ul << (BitsPerWord - 1 - (j % BitsPerWord)));
            }
        }

        masks_.push_back(std::move(mask));
    }
}

static bool test_collision(const Sprite &sprite1, const glm::vec2 &pos1, const Sprite &sprite2, const glm::vec2 &pos2)
{
    const auto pos = (1.0f / SpriteScale) * (tile_top_left(sprite1.tile, pos1) - tile_top_left(sprite2.tile, pos2));
    return sprite2.collides_with(sprite1, pos);
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
    void advance_one_tic();
    void advance_waves();
    void advance_foes();
    void advance_player();
    void advance_missiles();
    void spawn_missiles();

    const Level *cur_level_ = nullptr;

    struct ActiveWave
    {
        ActiveWave(const Wave *wave);
        const Wave *wave;
#ifdef DRAW_ACTIVE_TRAJECTORIES
        Geometry<std::tuple<glm::vec2>> geometry;
#endif
    };

    int width_;
    int height_;
    std::vector<std::unique_ptr<ActiveWave>> active_waves_;
    std::vector<Foe> foes_;
    std::vector<Missile> missiles_;
    Player player_;
    Sprite player_sprite_; // XXX for now
    Sprite foe_sprite_; // XXX for now
    Sprite missile_sprite_; // XXX for now
    float timestamp_ = 0.0f; // milliseconds
    int cur_tic_ = 0;
#ifdef DRAW_ACTIVE_TRAJECTORIES
    ShaderProgram trajectory_program_;
#endif
};

World::World(int width, int height)
    : width_(width)
    , height_(height)
    , player_sprite_(g_sprite_sheet->find_tile("player-0.png"))
    , foe_sprite_(g_sprite_sheet->find_tile("foe-small.png"))
    , missile_sprite_(g_sprite_sheet->find_tile("missile.png"))
{
    player_.position = glm::vec2(0.5f * width, 0.5f * height);

#ifdef DRAW_ACTIVE_TRAJECTORIES
    trajectory_program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
    trajectory_program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
    trajectory_program_.link();

    const auto projection_matrix = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
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

#if 0
    {
        const auto *wave = level->waves.front().get();
        Foe foe(wave);
        foe.position = {250.f, 220.f};
        foes_.push_back(foe);
    }
#else
    advance_waves();
#endif
}

void World::advance(float dt)
{
    timestamp_ += dt;
    constexpr auto MillisecondsPerTic = 1000.0f / TicsPerSecond;

    while (timestamp_ > MillisecondsPerTic)
    {
        timestamp_ -= MillisecondsPerTic;
        ++cur_tic_;
        advance_one_tic();
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

#ifdef DRAW_COLLISIONS
    {
        bool has_collisions = std::any_of(foes_.begin(), foes_.end(), [this](const Foe &foe) {
            return test_collision(foe_sprite_, foe.position, player_sprite_, player_.position);
        });
        if (has_collisions)
        {
            glClearColor(1, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
#endif

    g_sprite_batcher->start_batch();

    const auto *foe_tile = foe_sprite_.tile;
    for (const auto &foe : foes_)
    {
        const auto a = static_cast<float>(foe.damage_tics) / DamageFlashInterval;
        draw_tile(foe_tile, foe.position, glm::vec4(1.0f, 0.0f, 0.0f, a), 0);
    }

    const auto *missile_tile = missile_sprite_.tile;
    for (const auto &missile : missiles_)
        draw_tile(missile_tile, missile.position, 0);

    draw_tile(player_.frames[player_.cur_frame], player_.position, 0);

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

void World::advance_one_tic()
{
    advance_waves();
    advance_missiles();
    advance_foes();
    advance_player();
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
            if (foe.damage_tics > 0)
                --foe.damage_tics;
            ++it;
        }
    }
}

void World::spawn_missiles()
{
    missiles_.push_back({player_.position - static_cast<float>(SpriteScale) * glm::vec2(-9.5, 12.5)});
    missiles_.push_back({player_.position - static_cast<float>(SpriteScale) * glm::vec2(9.5, 12.5)});

    assert(player_.fire_tics == 0);
    player_.fire_tics = MissileSpawnInterval;
}

void World::advance_player()
{
    constexpr float speed = 2.0f;
    constexpr float Margin = 12;

    if ((g_dpad_state & DPad_Up) && player_.position.y > Margin)
        player_.position.y -= speed;
    if ((g_dpad_state & DPad_Down) && player_.position.y < height_ - Margin)
        player_.position.y += speed;
    if ((g_dpad_state & DPad_Left) && player_.position.x > Margin)
        player_.position.x -= speed;
    if ((g_dpad_state & DPad_Right) && player_.position.x < width_ - Margin)
        player_.position.x += speed;
    if ((g_dpad_state & DPad_Button) && player_.fire_tics == 0)
        spawn_missiles();

    if (player_.fire_tics)
        --player_.fire_tics;

    player_.cur_frame = (cur_tic_ / 4) % player_.frames.size();
}

void World::advance_missiles()
{
    constexpr float speed = 9.0f;

    const auto *missile_tile = missile_sprite_.tile;
    const auto *foe_tile = foe_sprite_.tile;

    const auto &missile_size = missile_tile->size;
    const float min_y = -SpriteScale * 0.5f * missile_size.y;

    auto it = missiles_.begin();
    while (it != missiles_.end())
    {
        auto &missile = *it;
        missile.position += glm::vec2(0.f, -speed);

        bool erase_missile = false;
        if (missile.position.y < min_y)
        {
            erase_missile = true;
        }
        else
        {
            for (auto &foe : foes_)
            {
                if (test_collision(missile_sprite_, missile.position, foe_sprite_, foe.position))
                {
                    erase_missile = true;
                    foe.damage_tics = DamageFlashInterval;
                }
            }
        }
        if (erase_missile)
        {
            it = missiles_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// TODO: replace asserts with proper error checking and reporting (... maybe)

static glm::vec2 parse_vec2(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    assert(array.Size() == 2);
    return glm::vec2(array[0].GetDouble(), array[1].GetDouble());
}

static PathSegment parse_path_segment(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    assert(array.Size() == 4);
    return {parse_vec2(array[0]), parse_vec2(array[1]), parse_vec2(array[2]), parse_vec2(array[3])};
}

static std::unique_ptr<Trajectory> parse_trajectory(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    Path path;
    path.reserve(array.Size());
    std::transform(array.begin(), array.end(), std::back_inserter(path), [](const rapidjson::Value &value) {
        return parse_path_segment(value);
    });
    return std::make_unique<Trajectory>(path);
}

static std::unique_ptr<Level> load_level(const std::string &filename)
{
    const auto json = load_file(filename);

    rapidjson::Document document;
    rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag>(json.data());
    assert(ok);

    auto level = std::make_unique<Level>();

    const auto trajectories = document["trajectories"].GetArray();
    for (const auto &value : trajectories)
    {
        level->trajectories.push_back(parse_trajectory(value));
    }

    const auto waves = document["waves"].GetArray();
    for (const auto &value : waves)
    {
        assert(value.IsObject());

        auto wave = std::make_unique<Wave>();
        wave->start_tic = value["start_tic"].GetInt();
        wave->spawn_interval = value["spawn_interval"].GetInt();
        wave->spawn_count = value["spawn_count"].GetInt();
        wave->foe_speed = value["foe_speed"].GetDouble();

        const auto trajectory_index = value["trajectory"].GetInt();
        assert(trajectory_index >= 0 && trajectory_index < level->trajectories.size());
        wave->trajectory = level->trajectories[trajectory_index].get();

        level->waves.push_back(std::move(wave));
    }

    return level;
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
        g_sprite_sheet = load_tilesheet("resources/tilesheets/sheet.json");

        g_sprite_batcher = new SpriteBatcher;
        g_sprite_batcher->set_view_rectangle(0, window_width, window_height, 0);

        {
            auto level = load_level("resources/levels/level-0.json");

            World world(window_width, window_height);
            world.initialize_level(level.get());

            while (!glfwWindowShouldClose(window))
            {
                update_dpad_state(window);

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                world.advance(1000.f / 60.f);
                world.render();

                glfwSwapBuffers(window);
                glfwPollEvents();
            }
        }

        delete g_sprite_batcher;
        g_sprite_sheet.reset();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
