#pragma once

#include <array>
#include <vector>
#include <memory>

#include <glm/vec2.hpp>

using QuadVerts = std::array<glm::vec2, 4>;

class Texture;
struct TileSheet;

struct Tile
{
    std::string name;
    QuadVerts texture_coords;
    const TileSheet *sheet;
};

struct TileSheet
{
    std::unique_ptr<Texture> texture;
    std::vector<std::unique_ptr<Tile>> tiles;
};

std::unique_ptr<TileSheet> load_tilesheet(const std::string &path);
