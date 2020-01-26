#pragma once

#include "collisionmask.h"

#include <glm/vec2.hpp>

#include <vector>
#include <memory>

#ifdef DRAW_ACTIVE_TRAJECTORIES
#include "geometry.h"
#include "shaderprogram.h"
#endif

struct Level;
struct Tile;
struct Wave;
class Trajectory;

struct Player
{
    Player();

    std::vector<const Tile *> frames;
    glm::vec2 position;
    int cur_frame = 0;
    int fire_tics = 0;
};

struct Missile
{
    glm::vec2 position;
};

struct Foe
{
    Foe(const Wave *wave);

    int type;
    float speed;
    const Trajectory *trajectory;
    glm::vec2 position;
    float trajectory_position;
    int damage_tics = 0;
    int cur_frame = 0;
    int cur_tic = 0;
};

class World
{
public:
    World(int window_width, int window_height);

    void initialize_level(const Level *level);
    void advance(unsigned dpad_state);
    void render() const;

private:
    void advance_waves();
    void advance_foes();
    void advance_player(unsigned dpad_state);
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
    CollisionMask player_sprite_; // XXX for now
    CollisionMask missile_sprite_; // XXX for now
    int cur_tic_ = 0;
#ifdef DRAW_ACTIVE_TRAJECTORIES
    ShaderProgram trajectory_program_;
#endif
};
