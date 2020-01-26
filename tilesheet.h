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

void cache_tilesheet(const std::string &path);
void release_tilesheets();

const Tile *get_tile(const std::string &name);
