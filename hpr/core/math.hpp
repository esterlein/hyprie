#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS

#include "imports/glm/glm/glm.hpp"
#include "imports/glm/glm/gtc/matrix_transform.hpp"
#include "imports/glm/glm/gtc/quaternion.hpp"
#include "imports/glm/glm/gtx/quaternion.hpp"
#include "imports/glm/glm/gtc/type_ptr.hpp"

#include "panic.hpp"


namespace hpr {


using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;


inline const float* ptr(const mat4& mtx)
{
	return glm::value_ptr(mtx);
}


inline const float* ptr(const vec3& vec)
{
	return glm::value_ptr(vec);
}

} // hpr


namespace hpr::math {


inline constexpr float projection_epsilon    = 1e-5f;
inline constexpr float determinant_tolerance = 1e-6f;
inline constexpr float screen_len_sq_epsilon = 1e-6f;
inline constexpr float magnitude_sq_epsilon  = 1e-8f;
inline constexpr float collinearity_epsilon  = 1e-8f;


inline constexpr size_t frustum_plane_count  = 6;


inline constexpr float aabb_plane_half_thickness = 0.01f;


static inline std::array<vec4, frustum_plane_count> frustum_planes(const mat4& mtx_VP)
{
	const vec4 row_0 = vec4(mtx_VP[0][0], mtx_VP[1][0], mtx_VP[2][0], mtx_VP[3][0]);
	const vec4 row_1 = vec4(mtx_VP[0][1], mtx_VP[1][1], mtx_VP[2][1], mtx_VP[3][1]);
	const vec4 row_2 = vec4(mtx_VP[0][2], mtx_VP[1][2], mtx_VP[2][2], mtx_VP[3][2]);
	const vec4 row_3 = vec4(mtx_VP[0][3], mtx_VP[1][3], mtx_VP[2][3], mtx_VP[3][3]);

	const vec4 left   = row_3 + row_0;
	const vec4 right  = row_3 - row_0;
	const vec4 bottom = row_3 + row_1;
	const vec4 top    = row_3 - row_1;
	const vec4 near   = row_3 + row_2;
	const vec4 far    = row_3 - row_2;

	return {left, right, bottom, top, near, far};
}


[[nodiscard]] static inline int32_t floor_div(int32_t value, int32_t divisor)
{
	HPR_ASSERT_MSG(divisor > 0, "[floor div] divisor <= 0");

	if (value >= 0) {
		return value / divisor;
	}

	return -(((-value) + divisor - 1) / divisor);
}


} // hpr::math

