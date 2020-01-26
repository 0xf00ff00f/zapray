#pragma once

#include <vector>

#include "collisionmask.h"

struct Tile;

struct FoeClass
{
    struct Frame
    {
        const Tile *tile;
        CollisionMask collision_mask;
    };
    std::vector<Frame> frames;
    int tics_per_frame;
};
