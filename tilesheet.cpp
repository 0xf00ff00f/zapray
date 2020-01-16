#include "tilesheet.h"

#include "texture.h"
#include "pixmap.h"
#include "util.h"

#include <rapidjson/document.h>

#include <cassert>

static std::unique_ptr<Texture> load_texture(const char *path)
{
    auto pm = load_pixmap_from_png(path);

    auto texture = std::make_unique<Texture>();
    texture->set_data(*pm);

    return texture;
}

static glm::vec2 parse_vec2(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    assert(array.Size() == 2);
    return glm::vec2(array[0].GetDouble(), array[1].GetDouble());
}

static QuadVerts parse_texture_coords(const rapidjson::Value &value)
{
    assert(value.IsArray());
    const auto array = value.GetArray();
    assert(array.Size() == 4);
    return {parse_vec2(array[0]), parse_vec2(array[1]), parse_vec2(array[2]), parse_vec2(array[3])};
}

static std::unique_ptr<Tile> parse_tile(const rapidjson::Value &value)
{
    auto tile = std::make_unique<Tile>();
    tile->name = value["name"].GetString();
    tile->texture_coords = parse_texture_coords(value["texuv"]);
    return tile;
}

std::unique_ptr<TileSheet> load_tilesheet(const std::string &filename)
{
    const auto json = load_file(filename);

    rapidjson::Document document;
    rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag>(json.data());
    assert(ok);

    auto sheet = std::make_unique<TileSheet>();

    sheet->texture = load_texture(document["texture"].GetString());

    const auto tiles = document["tiles"].GetArray();
    for (const auto &value : tiles)
    {
        auto tile = parse_tile(value);
        tile->sheet = sheet.get();
        sheet->tiles.push_back(std::move(tile));
    }

    return sheet;
}
