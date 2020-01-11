#include "pixmap.h"

#include "panic.h"

#include <boost/noncopyable.hpp>

#include <algorithm>
#include <png.h>

namespace
{
size_t pixel_size(Pixmap::PixelType type)
{
    switch (type)
    {
    case Pixmap::PixelType::Gray:
    default:
        return 1;

    case Pixmap::PixelType::GrayAlpha:
        return 2;

    case Pixmap::PixelType::RGB:
        return 3;

    case Pixmap::PixelType::RGBAlpha:
        return 4;
    }
}

Pixmap::PixelType to_pixel_type(png_byte png_color_type)
{
    switch (png_color_type)
    {
    case PNG_COLOR_TYPE_GRAY:
        return Pixmap::PixelType::Gray;

    case PNG_COLOR_TYPE_GRAY_ALPHA:
        return Pixmap::PixelType::GrayAlpha;

    case PNG_COLOR_TYPE_RGB:
        return Pixmap::PixelType::RGB;

    case PNG_COLOR_TYPE_RGBA:
        return Pixmap::PixelType::RGBAlpha;

    default:
        return Pixmap::PixelType::Unknown;
    }
}

class File : private boost::noncopyable
{
public:
    File(const std::string &path) : fp_(fopen(path.c_str(), "rb")) { }
    ~File() { fclose(fp_); }

    operator FILE *() const { return fp_; }
    operator bool() const { return fp_; }

private:
    FILE *fp_;
};
}

Pixmap::Pixmap(size_t width, size_t height, PixelType type)
    : width(width)
    , height(height)
    , type(type)
    , pixels(width * height * pixel_size(type))
{
}

size_t Pixmap::row_stride() const
{
    return pixel_size(type) * width;
}

std::unique_ptr<Pixmap> load_pixmap_from_png(const char *path)
{
    File file(path);
    if (!file)
        panic("failed to open %s\n", path);

    png_structp png_ptr;

    if (!(png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0)))
        panic("png_create_read_struct\n");

    png_infop info_ptr;
    if (!(info_ptr = png_create_info_struct(png_ptr)))
        panic("png_create_info_struct\n");

    if (setjmp(png_jmpbuf(png_ptr)))
        panic("png error?\n");

    png_init_io(png_ptr, file);

    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, 0);

    if (png_get_bit_depth(png_ptr, info_ptr) != 8)
        panic("invalid PNG bit depth\n");

    const size_t width = png_get_image_width(png_ptr, info_ptr);
    const size_t height = png_get_image_height(png_ptr, info_ptr);

    const png_byte png_color_type = png_get_color_type(png_ptr, info_ptr);
    const auto pixel_type = to_pixel_type(png_color_type);
    if (pixel_type == Pixmap::PixelType::Unknown)
        panic("invalid PNG color type: %x\n", png_color_type);

    auto pm = std::make_unique<Pixmap>(width, height, pixel_type);

    const png_bytep *rows = png_get_rows(png_ptr, info_ptr);

    auto *dest = pm->pixels.data();
    const auto stride = pm->row_stride();

    for (size_t i = 0; i < height; i++)
    {
        std::copy(rows[i], rows[i] + stride, dest);
        dest += stride;
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, 0);

    return pm;
}
