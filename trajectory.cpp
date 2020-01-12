#include "trajectory.h"

#include <glm/geometric.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <algorithm>
#include <iostream>

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

    const auto &v1 = *it;
    const auto &v0 = *std::prev(it);

    assert(distance >= v0.distance);
    assert(distance < v1.distance);
    const float t = (distance - v0.distance) / (v1.distance - v0.distance);

    return v0.position + t * (v1.position - v0.position);
}

