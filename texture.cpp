#include "texture.h"

#include "pixmap.h"

Texture::Texture(const char *path)
    : pixmap_(load_pixmap_from_png(path))
{
    glGenTextures(1, &id_);
    set_data(*pixmap_);
}

Texture::~Texture()
{
    glDeleteTextures(1, &id_);
}

void Texture::bind() const
{
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Texture::unbind()
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::set_data(const Pixmap &pm)
{
    bind();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pm.width, pm.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pm.pixels.data());

    unbind();
}

const Pixmap *Texture::pixmap() const
{
    return pixmap_.get();
}
