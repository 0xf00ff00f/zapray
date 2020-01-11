#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct Pixmap
{
    enum class PixelType
    {
        Gray,
        GrayAlpha,
        RGB,
        RGBAlpha,
        Unknown,
    };

    Pixmap(size_t width, size_t height, PixelType type);

    size_t row_stride() const;

    size_t width;
    size_t height;
    PixelType type;
    std::vector<uint8_t> pixels;
};

std::unique_ptr<Pixmap> load_pixmap_from_png(const char *path);
