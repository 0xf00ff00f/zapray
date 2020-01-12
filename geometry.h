#pragma once

#include <boost/noncopyable.hpp>

#include <glm/glm.hpp>
#include <GL/glew.h>

#include <tuple>
#include <vector>
#include <iostream>

namespace detail
{
template<typename T>
struct vertex_component_traits;

template<>
struct vertex_component_traits<float>
{
    static constexpr GLint size = 1;
    static constexpr GLenum type = GL_FLOAT;
};

template<>
struct vertex_component_traits<glm::vec2>
{
    static constexpr GLint size = 2;
    static constexpr GLenum type = GL_FLOAT;
};

template<>
struct vertex_component_traits<glm::vec3>
{
    static constexpr GLint size = 3;
    static constexpr GLenum type = GL_FLOAT;
};

template<>
struct vertex_component_traits<glm::vec4>
{
    static constexpr GLint size = 4;
    static constexpr GLenum type = GL_FLOAT;
};

template<typename T>
struct tuple_stride;

template<typename Head, typename... Ts>
struct tuple_stride<std::tuple<Head, Ts...>>
{
    static constexpr std::size_t value = sizeof(Head) + tuple_stride<std::tuple<Ts...>>::value;
};

template<>
struct tuple_stride<std::tuple<>>
{
    static constexpr std::size_t value = 0;
};

template<std::size_t Index, typename T>
struct tuple_element_offset;

template<typename Head, typename... Ts>
struct tuple_element_offset<0, std::tuple<Head, Ts...>>
{
    static constexpr std::size_t value = tuple_stride<std::tuple<Ts...>>::value;
};

template<std::size_t Index, typename Head, typename... Ts>
struct tuple_element_offset<Index, std::tuple<Head, Ts...>>
{
    static constexpr std::size_t value = tuple_element_offset<Index - 1, std::tuple<Ts...>>::value;
};

template<typename VertexT, std::size_t Index>
void declare_vertex_attrib_pointer_for()
{
    using attrib_type = typename std::tuple_element<Index, VertexT>::type;
    using attrib_traits = vertex_component_traits<attrib_type>;

    constexpr size_t stride = tuple_stride<VertexT>::value;
    constexpr size_t offset = tuple_element_offset<Index, VertexT>::value;

    glEnableVertexAttribArray(Index);
    glVertexAttribPointer(Index, attrib_traits::size, attrib_traits::type, GL_FALSE, stride,
                          reinterpret_cast<GLvoid *>(offset));
}

template<typename VertexT, std::size_t... Indexes>
void declare_vertex_attrib_pointers_impl(std::index_sequence<Indexes...>)
{
    std::initializer_list<int>{(declare_vertex_attrib_pointer_for<VertexT, Indexes>(), 0)...};
}

template<typename... Ts>
void declare_vertex_attrib_pointers(std::tuple<Ts...>)
{
    using vertex_type = std::tuple<Ts...>;
    static_assert(sizeof(vertex_type) == tuple_stride<vertex_type>::value);
    declare_vertex_attrib_pointers_impl<vertex_type>(std::index_sequence_for<Ts...>{});
}
}

template<typename VertexT>
class Geometry : private boost::noncopyable
{
public:
    Geometry()
    {
        glGenBuffers(1, &vbo_);
        glGenVertexArrays(1, &vao_);
    }

    ~Geometry()
    {
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
    }

    void set_data(const std::vector<VertexT> &verts)
    {
        set_data(verts.data(), verts.size());
    }

    void set_data(const VertexT *vert_data, int vert_count)
    {
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(VertexT) * vert_count, vert_data, GL_STATIC_DRAW);

        detail::declare_vertex_attrib_pointers(VertexT{});

        vert_count_ = vert_count;
    }

    VertexT *map_vertex_data()
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        return reinterpret_cast<VertexT *>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));
    }

    static void unmap_vertex_data()
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void bind() const { glBindVertexArray(vao_); }

    void render(GLenum mode) const
    {
        render(mode, vert_count_);
    }

    void render(GLenum mode, int vert_count) const
    {
        bind();
        glDrawArrays(mode, 0, vert_count);
    }

private:
    GLuint vao_;
    GLuint vbo_;
    std::size_t vert_count_;
};