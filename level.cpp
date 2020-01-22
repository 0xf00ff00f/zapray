#include "level.h"

#include "trajectory.h"
#include "util.h"

#include <algorithm>

#include <glm/vec2.hpp>

#include <rapidjson/document.h>

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

std::unique_ptr<Level> load_level(const std::string &path)
{
    const auto json = load_file(path);

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
