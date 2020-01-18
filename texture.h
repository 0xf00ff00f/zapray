#pragma once

#include <memory>

#include <GL/glew.h>

#include <boost/noncopyable.hpp>

struct Pixmap;

class Texture : private boost::noncopyable
{
public:
    Texture(const char *path);
    ~Texture();

    void bind() const;
    static void unbind();

    const Pixmap *pixmap() const;

private:
    void set_data(const Pixmap &pixmap);

    GLuint id_;
    std::unique_ptr<Pixmap> pixmap_;
};
