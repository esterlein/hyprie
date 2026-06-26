#pragma once

#include "hprint.hpp"

#include <limits>

#include "math.hpp"
#include "entity.hpp"
#include "scene.hpp"
#include "render_data.hpp"
#include "raycast_data.hpp"
#include "vertex_mass.hpp"
#include "storage_mass.hpp"
#include "vertex_format.hpp"
#include "draw_view_data.hpp"
#include "render_context.hpp"


namespace hpr::scn {


inline Ray make_pick_ray(
	float                mouse_x,
	float                mouse_y,
	uint32_t             viewport_width,
	uint32_t             viewport_height,
	const rdr::DrawView& draw_view
)
{
	const float pos_ndc_x = 2.0f * mouse_x / static_cast<float>(viewport_width)  - 1.0f;
	const float pos_ndc_y = 1.0f - mouse_y / static_cast<float>(viewport_height) * 2.0f;

	const mat4 mtx_inv_VP = glm::inverse(draw_view.mtx_VP);

	const vec4 pos_clip_near {pos_ndc_x, pos_ndc_y, -1.0f, 1.0f};
	const vec4 pos_clip_far  {pos_ndc_x, pos_ndc_y,  1.0f, 1.0f};

	vec4 pos_world_near_hom = mtx_inv_VP * pos_clip_near;
	vec4 pos_world_far_hom  = mtx_inv_VP * pos_clip_far;

	const vec3 pos_world_near {pos_world_near_hom / pos_world_near_hom.w};
	const vec3 pos_world_far  {pos_world_far_hom  / pos_world_far_hom.w};

	Ray ray {
		.origin    = pos_world_near,
		.direction = glm::normalize(pos_world_far - pos_world_near)
	};

	return ray;
}


inline bool intersect_ray_aabb(
	const vec3& ray_origin,
	const vec3& ray_dir,
	const vec3& aabb_min,
	const vec3& aabb_max,
	float&      aabb_entry_dist
)
{
	float aabb_entry = 0.0f;
	float aabb_exit  = std::numeric_limits<float>::infinity();

	for (int axis_index = 0; axis_index < 3; ++axis_index) {
		const float inv_dir = 1.0f / ray_dir[axis_index];

		float entry_axis =
			(aabb_min[axis_index] - ray_origin[axis_index]) * inv_dir;
		float exit_axis  =
			(aabb_max[axis_index] - ray_origin[axis_index]) * inv_dir;

		if (entry_axis > exit_axis) {
			const float temp_distance = entry_axis;
			entry_axis = exit_axis;
			exit_axis = temp_distance;
		}

		aabb_entry = entry_axis > aabb_entry ? entry_axis : aabb_entry;
		aabb_exit  = exit_axis  < aabb_exit  ? exit_axis  : aabb_exit;

		if (aabb_exit < aabb_entry) {
			return false;
		}
	}

	aabb_entry_dist = aabb_entry;
	return true;
}


template <typename Func>
void for_each_triangle(
	const rdr::SceneVertex*    vtx_data,
	const uint32_t*            idx_data,
	const scn::ScenePrimitive& primitive,
	const mat4&                mtx_W,
	Func&&                     func
)
{
	const rdr::Submesh& submesh = primitive.submesh;

	for (uint32_t i = 0; i < submesh.idx_count; i += 3) {

		uint32_t idx0 = idx_data[submesh.idx_first + i + 0];
		uint32_t idx1 = idx_data[submesh.idx_first + i + 1];
		uint32_t idx2 = idx_data[submesh.idx_first + i + 2];

		const auto& vtx0 = vtx_data[submesh.vtx_base + idx0];
		const auto& vtx1 = vtx_data[submesh.vtx_base + idx1];
		const auto& vtx2 = vtx_data[submesh.vtx_base + idx2];

		vec3 vtx0_W = vec3(mtx_W * vec4(vtx0.pos.x, vtx0.pos.y, vtx0.pos.z, 1.0f));
		vec3 vtx1_W = vec3(mtx_W * vec4(vtx1.pos.x, vtx1.pos.y, vtx1.pos.z, 1.0f));
		vec3 vtx2_W = vec3(mtx_W * vec4(vtx2.pos.x, vtx2.pos.y, vtx2.pos.z, 1.0f));

		func(vtx0_W, vtx1_W, vtx2_W);
	}
}


inline bool intersect_ray_triangle(
	const vec3& ray_origin,
	const vec3& ray_direction,
	const vec3& vertex_A,
	const vec3& vertex_B,
	const vec3& vertex_C,
	float&      hit_distance,
	float&      barycentric_U,
	float&      barycentric_V
)
{
	const vec3 edge_AB = vertex_B - vertex_A;
	const vec3 edge_AC = vertex_C - vertex_A;

	const vec3 p_vec = glm::cross(ray_direction, edge_AC);
	const float det  = glm::dot(edge_AB, p_vec);

	if (det > -math::parallel_epsilon && det < math::parallel_epsilon) {
		return false;
	}

	const float inv_det = 1.0f / det;
	const vec3 t_vec    = ray_origin - vertex_A;

	barycentric_U = glm::dot(t_vec, p_vec) * inv_det;
	if (barycentric_U < 0.0f || barycentric_U > 1.0f) {
		return false;
	}

	const vec3 q_vec = glm::cross(t_vec, edge_AB);
	barycentric_V = glm::dot(ray_direction, q_vec) * inv_det;
	if (barycentric_V < 0.0f || barycentric_U + barycentric_V > 1.0f) {
		return false;
	}

	hit_distance = glm::dot(edge_AC, q_vec) * inv_det;

	return hit_distance > math::parallel_epsilon;
}


inline bool intersect_ray_plane_y(
	const Ray& ray,
	float      plane_y,
	float&     hit_distance,
	vec3&      hit_position_world
)
{
	const float denominator = ray.direction.y;

	if (fabsf(denominator) <= std::numeric_limits<float>::epsilon()) {
		return false;
	}

	hit_distance = (plane_y - ray.origin.y) / denominator;

	if (hit_distance < 0.0f) {
		return false;
	}

	hit_position_world = ray.origin + ray.direction * hit_distance;

	return true;
}


inline bool intersect_ray_ground_plane(
	const Ray& ray,
	float&     hit_distance,
	vec3&      hit_position_world
)
{
	return intersect_ray_plane_y(ray, 0.0f, hit_distance, hit_position_world);
}


} // hpr::scn

