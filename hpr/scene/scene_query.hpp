#pragma once

#include <limits>
#include <cstdint>

#include "math.hpp"
#include "entity.hpp"
#include "scene.hpp"
#include "render_data.hpp"
#include "draw_view_data.hpp"
#include "components_scene.hpp"
#include "components_render.hpp"
#include "scene_resolver.hpp"


namespace hpr::scn {


struct Ray
{
	vec3 origin;
	vec3 direction;
};


struct RayHit
{
	bool        hit                  {false};
	ecs::Entity entity               {ecs::invalid_entity};
	uint32_t    submesh              {std::numeric_limits<uint32_t>::max()};
	float       closest_hit_distance {std::numeric_limits<float>::infinity()};
};


inline bool intersect_ray_aabb(
	const vec3& ray_origin,
	const vec3& ray_direction,
	const vec3& aabb_min,
	const vec3& aabb_max,
	float&      aabb_entry_distance
)
{
	float aabb_entry = 0.0f;
	float aabb_exit  = std::numeric_limits<float>::infinity();

	for (int axis_index = 0; axis_index < 3; ++axis_index) {
		const float inverse_direction = 1.0f / ray_direction[axis_index];

		float entry_axis = (aabb_min[axis_index] - ray_origin[axis_index]) * inverse_direction;
		float exit_axis  = (aabb_max[axis_index] - ray_origin[axis_index]) * inverse_direction;

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

	aabb_entry_distance = aabb_entry;
	return true;
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
	const vec3 edge_vec_AB = vertex_B - vertex_A;
	const vec3 edge_vec_AC = vertex_C - vertex_A;

	const vec3 dir_cross_edge_AC = glm::cross(ray_direction, edge_vec_AC);
	const float determinant = glm::dot(edge_vec_AB, dir_cross_edge_AC);

	const float edge_AB_len = glm::length(edge_vec_AB);
	const float edge_AC_len = glm::length(edge_vec_AC);
	const float det_epsilon = std::numeric_limits<float>::epsilon() * (edge_AB_len * edge_AC_len);

	if (fabsf(determinant) <= det_epsilon) {
		return false;
	}

	const float inverse_determinant = 1.0f / determinant;
	const vec3 origin_vtx_A = ray_origin - vertex_A;

	barycentric_U = glm::dot(origin_vtx_A, dir_cross_edge_AC) * inverse_determinant;
	if (barycentric_U < 0.0f || barycentric_U > 1.0f) {
		return false;
	}

	const vec3 origin_vtx_A_cross_edge_AB = glm::cross(origin_vtx_A, edge_vec_AB);

	barycentric_V = glm::dot(ray_direction, origin_vtx_A_cross_edge_AB) * inverse_determinant;
	if (barycentric_V < 0.0f || barycentric_V + barycentric_U > 1.0f) {
		return false;
	}

	hit_distance = glm::dot(edge_vec_AC, origin_vtx_A_cross_edge_AB) * inverse_determinant;
	return hit_distance > 0.0f;
}


inline bool intersect_ray_plane_y(const Ray& ray, float plane_y, float& hit_distance, vec3& hit_position_world)
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


inline bool intersect_ray_ground_plane(const Ray& ray, float& hit_distance, vec3& hit_position_world)
{
	return intersect_ray_plane_y(ray, 0.0f, hit_distance, hit_position_world);
}


template <typename Registry, typename Fn>
void for_each_pickable_entity(Registry& registry, Fn&& callback)
{
	registry.template scan<ecs::ModelComponent, ecs::BoundComponent>(
		[&callback](ecs::Entity entity, const ecs::ModelComponent&, const ecs::BoundComponent& bound_component)
		{
			const vec3 aabb_min = bound_component.world_center - bound_component.world_half;
			const vec3 aabb_max = bound_component.world_center + bound_component.world_half;

			callback(entity, aabb_min, aabb_max);
		});
}


template <typename Registry, typename Fn>
void for_each_triangle(
	Registry&                 registry,
	scn::Scene&               scene,
	const scn::SceneResolver& resolver,
	ecs::Entity               entity,
	uint32_t                  local_submesh_index,
	Fn&&                      callback
)
{
	const auto* model_component = registry.template get<ecs::ModelComponent>(entity);
	if (!model_component) {
		return;
	}

	const uint32_t absolute_primitive_index = model_component->submesh_first + local_submesh_index;
	const auto& scene_primitives = scene.scene_primitives();

	if (absolute_primitive_index >= scene_primitives.size()) {
		return;
	}

	const auto& scene_primitive = scene_primitives[absolute_primitive_index];

	auto* mesh = resolver.template resolve<rdr::Mesh>(scene_primitive.mesh);
	if (!mesh) {
		return;
	}

	const uint32_t mesh_submesh_index = scene_primitive.submesh_idx;
	if (mesh_submesh_index >= mesh->submeshes.size()) {
		return;
	}

	auto* mesh_geometry = resolver.template resolve<rdr::MeshGeometry>(mesh->geometry);
	if (!mesh_geometry) {
		return;
	}
	if (mesh_submesh_index >= mesh_geometry->submeshes.size()) {
		return;
	}

	const rdr::Submesh& render_submesh = mesh->submeshes[mesh_submesh_index];
	const rdr::SubmeshGeometry& geometry_submesh = mesh_geometry->submeshes[mesh_submesh_index];

	const uint8_t* vertex_bytes = mesh_geometry->vertex_bytes.data();
	const uint32_t* index_data = reinterpret_cast<const uint32_t*>(geometry_submesh.index_bytes.data());

	const uint32_t triangle_index_start = render_submesh.first_idx;
	const uint32_t triangle_index_count = render_submesh.idx_count;

	const auto* transform_component = registry.template get<ecs::TransformComponent>(entity);

	for (uint32_t index_offset = 0; index_offset + 2 < triangle_index_count; index_offset += 3) {
		const uint32_t idx_0 = index_data[triangle_index_start + index_offset + 0];
		const uint32_t idx_1 = index_data[triangle_index_start + index_offset + 1];
		const uint32_t idx_2 = index_data[triangle_index_start + index_offset + 2];

		const rdr::SceneVertex* vtx_0 = reinterpret_cast<const rdr::SceneVertex*>(vertex_bytes + sizeof(rdr::SceneVertex) * idx_0);
		const rdr::SceneVertex* vtx_1 = reinterpret_cast<const rdr::SceneVertex*>(vertex_bytes + sizeof(rdr::SceneVertex) * idx_1);
		const rdr::SceneVertex* vtx_2 = reinterpret_cast<const rdr::SceneVertex*>(vertex_bytes + sizeof(rdr::SceneVertex) * idx_2);

		vec3 pos_world_0;
		vec3 pos_world_1;
		vec3 pos_world_2;

		if (transform_component) {
			const vec3 pos_loc_scaled_0 = vtx_0->pos * transform_component->scale;
			const vec3 pos_loc_scaled_1 = vtx_1->pos * transform_component->scale;
			const vec3 pos_loc_scaled_2 = vtx_2->pos * transform_component->scale;

			const vec3 pos_loc_rotated_0 = glm::rotate(transform_component->rotation, pos_loc_scaled_0);
			const vec3 pos_loc_rotated_1 = glm::rotate(transform_component->rotation, pos_loc_scaled_1);
			const vec3 pos_loc_rotated_2 = glm::rotate(transform_component->rotation, pos_loc_scaled_2);

			pos_world_0 = transform_component->position + pos_loc_rotated_0;
			pos_world_1 = transform_component->position + pos_loc_rotated_1;
			pos_world_2 = transform_component->position + pos_loc_rotated_2;
		}
		else {
			pos_world_0 = vtx_0->pos;
			pos_world_1 = vtx_1->pos;
			pos_world_2 = vtx_2->pos;
		}

		callback(pos_world_0, pos_world_1, pos_world_2);
	}
}


template <typename Registry>
RayHit raycast_scene(Ray& ray, Registry& registry, scn::Scene& scene, const scn::SceneResolver& resolver)
{
	const vec3 ray_direction = glm::normalize(ray.direction);

	RayHit best_hit {};

	// TODO: replace with BVH traversal

	for_each_pickable_entity(registry, [&ray, &ray_direction, &registry, &scene, &resolver, &best_hit](
		ecs::Entity entity,
		const vec3& entity_aabb_min,
		const vec3& entity_aabb_max
	)
	{
		float aabb_entry_distance = 0.0f;

		if (!intersect_ray_aabb(ray.origin, ray_direction, entity_aabb_min, entity_aabb_max, aabb_entry_distance)) {
			return;
		}

		const auto* model_component = registry.template get<ecs::ModelComponent>(entity);
		if (!model_component) {
			return;
		}
		const uint32_t submesh_count = model_component->submesh_count;

		for (uint32_t submesh_index = 0; submesh_index < submesh_count; ++submesh_index) {

			float closest_hit_distance_for_submesh = std::numeric_limits<float>::infinity();

			for_each_triangle(
				registry,
				scene,
				resolver,
				entity,
				submesh_index,
				[&ray, &ray_direction, &closest_hit_distance_for_submesh](
					const vec3& vtx_world_0,
					const vec3& vtx_world_1,
					const vec3& vtx_world_2
				)
				{
					float barycentric_U;
					float barycentric_V;
					float triangle_hit_distance;

					if (intersect_ray_triangle(
						ray.origin,
						ray_direction,
						vtx_world_0,
						vtx_world_1,
						vtx_world_2,
						triangle_hit_distance,
						barycentric_U,
						barycentric_V
					)) {
						if (triangle_hit_distance < closest_hit_distance_for_submesh) {
							closest_hit_distance_for_submesh = triangle_hit_distance;
						}
					}
				}
			);

			if (closest_hit_distance_for_submesh < best_hit.closest_hit_distance) {
				best_hit.hit = true;
				best_hit.entity = entity;
				best_hit.submesh = submesh_index;
				best_hit.closest_hit_distance = closest_hit_distance_for_submesh;
			}
		}
	});

	return best_hit;
}


inline Ray make_pick_ray(
	float                mouse_x,
	float                mouse_y,
	uint32_t             viewport_width,
	uint32_t             viewport_height,
	const rdr::DrawView& draw_view
)
{
	const float pos_ndc_x = (2.0f * mouse_x) / static_cast<float>(viewport_width)  - 1.0f;
	const float pos_ndc_y = 1.0f - (2.0f * mouse_y) / static_cast<float>(viewport_height);

	const mat4 mtx_inv_VP = glm::inverse(draw_view.mtx_VP);

	const vec4 pos_clip_near {pos_ndc_x, pos_ndc_y, -1.0f, 1.0f};
	const vec4 pos_clip_far  {pos_ndc_x, pos_ndc_y,  1.0f, 1.0f};

	vec4 pos_world_near_hom = mtx_inv_VP * pos_clip_near;
	vec4 pos_world_far_hom  = mtx_inv_VP * pos_clip_far;

	pos_world_near_hom /= pos_world_near_hom.w;
	pos_world_far_hom  /= pos_world_far_hom.w;

	const vec3 pos_world_near {pos_world_near_hom}; // NOTE: keep for now
	const vec3 pos_world_far  {pos_world_far_hom};

	Ray ray {
		.origin    = draw_view.pos_world,
		.direction = glm::normalize(pos_world_far - draw_view.pos_world)
	};

	return ray;
}

} // hpr::scn

