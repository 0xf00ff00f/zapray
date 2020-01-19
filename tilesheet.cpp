#include "tilesheet.h"

#include "texture.h"
#include "pixmap.h"
#include "util.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <cassert>

static glm::ivec2 parse_ivec2(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    assert(array.Size() == 2);
    return {array[0].GetInt(), array[1].GetInt()};
}

static std::unique_ptr<Tile> parse_tile(const rapidjson::Value &value, const std::vector<std::unique_ptr<Texture>> &textures)
{
    auto tile = std::make_unique<Tile>();
    tile->name = value["name"].GetString();
    tile->position = parse_ivec2(value["position"]);
    tile->size = parse_ivec2(value["size"]);

    const auto texture_index = value["texture"].GetInt();
    assert(texture_index >= 0 && texture_index < textures.size());
    tile->texture = textures[texture_index].get();

    const auto *pm = tile->texture->pixmap();

    const auto texture_width = pm->width;
    const auto texture_height = pm->height;

    const float u = static_cast<float>(tile->position.x) / texture_width;
    const float v = static_cast<float>(tile->position.y) / texture_height;

    const float du = static_cast<float>(tile->size.x) / texture_width;
    const float dv = static_cast<float>(tile->size.y) / texture_height;

    tile->tex_coords[0] = {u, v};
    tile->tex_coords[1] = {u, v + dv};
    tile->tex_coords[2] = {u + du, v + dv};
    tile->tex_coords[3] = {u + du, v};

    return tile;
}

std::unique_ptr<TileSheet> load_tilesheet(const std::string &filename)
{
    const auto json = load_file(filename);

    rapidjson::Document document;
    rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag>(json.data());
    assert(ok);

    auto sheet = std::make_unique<TileSheet>();

    const auto textures = document["textures"].GetArray();
    for (const auto &texture_path : textures)
    {
        sheet->textures.emplace_back(new Texture(texture_path.GetString()));
    }

    const auto tiles = document["sprites"].GetArray();
    for (const auto &value : tiles)
    {
        sheet->tiles.push_back(parse_tile(value, sheet->textures));
    }

    return sheet;
}

const Tile *TileSheet::find_tile(std::string_view name) const
{
    auto it = std::find_if(tiles.begin(), tiles.end(), [name](const auto &tile) {
        return tile->name == name;
    });
    return it != tiles.end() ? it->get() : nullptr;
}
