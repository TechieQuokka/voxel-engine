#pragma once

// This is the only file in the project that includes GLM. Everything else uses
// the aliases below, so replacing the math library means changing one file.

#define GLM_FORCE_RADIANS
#define GLM_FORCE_XYZW_ONLY
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace mc {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;
using ivec4 = glm::ivec4;

using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;

using mat3 = glm::mat3;
using mat4 = glm::mat4;

using quat = glm::quat;

namespace math = glm;

} // namespace mc
