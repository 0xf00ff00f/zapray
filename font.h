#pragma once

#include <glm/mat4x4.hpp>
#include <string_view>

void render_text(const glm::mat4 &mvp, float x, float y, const std::string_view text);
