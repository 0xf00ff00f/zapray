#pragma once

#include <vector>
#include <memory>

class Trajectory;

struct Wave
{
    int foe_type;
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

std::unique_ptr<Level> load_level(const std::string &path);
