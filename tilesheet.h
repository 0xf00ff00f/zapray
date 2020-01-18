#pragma once

#include <array>
#include <vector>
#include <memory>

#include <glm/vec2.hpp>

using QuadVerts = std::array<glm::vec2, 4>;

class Texture;

struct Tile
{
    std::string name;
    glm::ivec2 size;
    glm::ivec2 position;
    QuadVerts tex_coords;
    const Texture *texture;
};

struct TileSheet
{
    std::vector<std::unique_ptr<Texture>> textures;
    std::vector<std::unique_ptr<Tile>> tiles;

    const Tile *find_tile(std::string_view name) const;
};

std::unique_ptr<TileSheet> load_tilesheet(const std::string &path);
