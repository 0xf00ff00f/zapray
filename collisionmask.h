#pragma once

#include <glm/vec2.hpp>
#include <vector>

struct Tile;

class CollisionMask
{
public:
    CollisionMask(const Tile *tile);

    const Tile *tile;

    bool collides_with(const CollisionMask &other, const glm::vec2 &pos) const;

private:
    void initialize_mask();

    using Word = uint64_t;
    static constexpr const auto BitsPerWord = 8 * sizeof(Word);
    using Bitmask = std::vector<Word>;

    bool test_bitmasks(const Bitmask &mask0, const Bitmask &mask1, int offset) const;

    std::vector<Bitmask> masks_;
};
