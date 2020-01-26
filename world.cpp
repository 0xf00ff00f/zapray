#include "world.h"

#include "tilesheet.h"
#include "trajectory.h"
#include "level.h"
#include "spritebatcher.h"
#include "dpadstate.h"

#include <glm/vec4.hpp>
#include <algorithm>

#define DRAW_COLLISIONS

extern SpriteBatcher *g_sprite_batcher; // XXX

static constexpr const auto SpriteScale = 2.0f;
static constexpr const auto MissileSpawnInterval = 8;
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

static bool test_collision(const CollisionMask &sprite1, const glm::vec2 &pos1, const CollisionMask &sprite2, const glm::vec2 &pos2)
{
    const auto pos = (1.0f / SpriteScale) * (tile_top_left(sprite1.tile, pos1) - tile_top_left(sprite2.tile, pos2));
    return sprite2.collides_with(sprite1, pos);
}

Player::Player()
{
    const auto frame_tiles = {"player-0.png", "player-1.png", "player-2.png", "player-3.png"};
    for (const auto tile : frame_tiles)
        frames.push_back(get_tile(tile));
}

Foe::Foe(const Wave *wave)
    : speed(wave->foe_speed)
    , trajectory(wave->trajectory)
    , position(trajectory->point_at(0.0f))
    , trajectory_position(0.0f)
{
}

World::World(int width, int height)
    : width_(width)
    , height_(height)
    , player_sprite_(get_tile("player-0.png"))
    , foe_sprite_(get_tile("foe-small.png"))
    , missile_sprite_(get_tile("missile.png"))
{
    player_.position = glm::vec2(0.5f * width, 0.5f * height);

#ifdef DRAW_ACTIVE_TRAJECTORIES
    trajectory_program_.add_shader(GL_VERTEX_SHADER, "resources/shaders/dummy.vert");
    trajectory_program_.add_shader(GL_FRAGMENT_SHADER, "resources/shaders/dummy.frag");
    trajectory_program_.link();
#endif
}

void World::initialize_level(const Level *level)
{
    cur_level_ = level;

    foes_.clear();
    active_waves_.clear();

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

void World::render() const
{
#ifdef DRAW_ACTIVE_TRAJECTORIES
    trajectory_program_.bind();
    trajectory_program_.set_uniform(trajectory_program_.uniform_location("mvp"), g_sprite_batcher->transform_matrix());

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

void World::advance(unsigned dpad_state)
{
    ++cur_tic_;
    advance_waves();
    advance_missiles();
    advance_foes();
    advance_player(dpad_state);
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

void World::advance_player(unsigned dpad_state)
{
    constexpr float Speed = 2.0f;
    constexpr float Margin = 12;

    if ((dpad_state & DPad_Up) && player_.position.y > Margin)
        player_.position.y -= Speed;
    if ((dpad_state & DPad_Down) && player_.position.y < height_ - Margin)
        player_.position.y += Speed;
    if ((dpad_state & DPad_Left) && player_.position.x > Margin)
        player_.position.x -= Speed;
    if ((dpad_state & DPad_Right) && player_.position.x < width_ - Margin)
        player_.position.x += Speed;
    if ((dpad_state & DPad_Button) && player_.fire_tics == 0)
        spawn_missiles();

    if (player_.fire_tics)
        --player_.fire_tics;

    player_.cur_frame = (cur_tic_ / 4) % player_.frames.size();
}

void World::advance_missiles()
{
    constexpr float speed = 18.0f;

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
