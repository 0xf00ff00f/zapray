#pragma once

#include <GL/glew.h>

#include <boost/noncopyable.hpp>

struct Pixmap;

class Texture : private boost::noncopyable
{
public:
    Texture();
    ~Texture();

    void bind() const;
    static void unbind();

    void set_data(const Pixmap &pixmap);

private:
    GLuint id_;
};
