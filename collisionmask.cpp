#include "collisionmask.h"

#include "tilesheet.h"
#include "texture.h"
#include "pixmap.h"

CollisionMask::CollisionMask(const Tile *tile)
    : tile(tile)
{
    initialize_mask();
}

bool CollisionMask::collides_with(const CollisionMask &other, const glm::vec2 &pos) const
{
    const auto cols = tile->size.x;
    const auto rows = tile->size.y;

    const auto other_cols = other.tile->size.x;
    const auto other_rows = other.tile->size.y;

    const auto row_offset = static_cast<int>(pos.y);
    const auto col_offset = static_cast<int>(pos.x);

    if (col_offset >= cols || col_offset < -other_cols)
        return false;

    if (row_offset >= rows || row_offset < -other_rows)
        return false;

    assert(masks_.size() == rows);

    const int start_row = std::max(0, row_offset);
    const int end_row = std::min(rows, row_offset + other_rows);

    for (int row = start_row; row < end_row; ++row)
    {
        const auto other_row = row - row_offset;
        assert(other_row >= 0 && other_row < other_rows);

        const auto &mask = masks_[row];
        const auto &other_mask = other.masks_[other_row];

        if (col_offset >= 0)
        {
            if (test_bitmasks(mask, other_mask, col_offset))
                return true;
        }
        else
        {
            if (test_bitmasks(other_mask, mask, -col_offset))
                return true;
        }
    }

    return false;
}

bool CollisionMask::test_bitmasks(const Bitmask &mask0, const Bitmask &mask1, int shift) const
{
    const auto word_offset = shift / BitsPerWord;
    const auto word_shift = shift % BitsPerWord;

    for (int i = 0, j = word_offset; i < mask1.size() && j < mask0.size(); ++i, ++j)
    {
        const auto w0 = mask1[i];

        auto w1 = mask0[j] << word_shift;
        if (j + 1 < mask0.size())
            w1 |= (mask0[j + 1] >> (BitsPerWord - word_shift));

        if (w0 & w1)
            return true;
    }

    return false;
}

void CollisionMask::initialize_mask()
{
    const auto *pm = tile->texture->pixmap();
    assert(pm->type == Pixmap::PixelType::RGBAlpha); // XXX for now

    const auto *pixels = reinterpret_cast<const uint32_t *>(pm->pixels.data());

    masks_.reserve(tile->size.y);

    for (int i = 0; i < tile->size.y; ++i)
    {
        const auto mask_words = (tile->size.x + BitsPerWord - 1) / BitsPerWord;

        Bitmask mask(mask_words, 0);

        for (int j = 0; j < tile->size.x; ++j)
        {
            const uint32_t pixel = pixels[(i + tile->position.y) * pm->width + j + tile->position.x];
            if ((pixel >> 24) > 0x7f)
            {
                mask[j / BitsPerWord] |= (1ul << (BitsPerWord - 1 - (j % BitsPerWord)));
            }
        }

        masks_.push_back(std::move(mask));
    }
}

