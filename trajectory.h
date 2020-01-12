#pragma once

#include <glm/vec2.hpp>
#include <vector>
#include <array>

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
