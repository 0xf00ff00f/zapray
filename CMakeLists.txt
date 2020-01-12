cmake_minimum_required(VERSION 2.8.12)
project(Test)

add_definitions("-std=c++17")

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(demo
    main.cpp
    trajectory.cpp
    pixmap.cpp
    texture.cpp
    shaderprogram.cpp
    spritebatcher.cpp)

target_link_libraries(demo ${CONAN_LIBS})
