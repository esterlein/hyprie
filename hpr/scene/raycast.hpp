#pragma once

#include "hprint.hpp"

#include "math.hpp"
#include "entity.hpp"
#include "scene_rig.hpp"
#include "render_data.hpp"
#include "raycast_data.hpp"
#include "vertex_mass.hpp"
#include "storage_mass.hpp"
#include "vertex_format.hpp"
#include "draw_view_data.hpp"
#include "render_context.hpp"

#include <limits>
#include <immintrin.h>


namespace hpr::scn {


inline __m256 load_u8_to_ps256(const uint8_t* ptr)
{
	__m128i packed_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(ptr));
	return _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(packed_bytes));
}


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


inline bool intersect_ray_triangle(
	const vec3& ray_origin,
	const vec3& ray_direction,
	const vec3& vtx_0,
	const vec3& vtx_1,
	const vec3& vtx_2,
	float&      hit_distance,
	float&      u_bary,
	float&      v_bary
)
{
	const vec3 edge_AB = vtx_1 - vtx_0;
	const vec3 edge_AC = vtx_2 - vtx_0;

	const vec3 p_vec = glm::cross(ray_direction, edge_AC);
	const float det  = glm::dot(edge_AB, p_vec);

	if (det > -math::parallel_epsilon && det < math::parallel_epsilon) {
		return false;
	}

	const float inv_det = 1.0f / det;
	const vec3 t_vec    = ray_origin - vtx_0;

	u_bary = glm::dot(t_vec, p_vec) * inv_det;
	if (u_bary < 0.0f || u_bary > 1.0f) {
		return false;
	}

	const vec3 q_vec = glm::cross(t_vec, edge_AB);
	v_bary = glm::dot(ray_direction, q_vec) * inv_det;
	if (v_bary < 0.0f || u_bary + v_bary > 1.0f) {
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


inline RayHit raycast_scene(
	const Ray&                 ray,
	const Scene&               scene,
	const rdr::StagingContext& staging_ctx
)
{
	RayHit closest_hit {};

	const auto& spatial_rig = scene.spatial_rig;
	const auto& rdr_rig     = scene.render_rig;

	if (spatial_rig.tlas_nodes.empty()) {
		return closest_hit;
	}

	const vec3 dir_inv {

		1.0f / (fabsf(ray.direction.x) < math::zero_div_epsilon
			? (std::signbit(ray.direction.x)
				? -math::zero_div_epsilon
				:  math::zero_div_epsilon)
			: ray.direction.x
		),

		1.0f / (fabsf(ray.direction.y) < math::zero_div_epsilon
			? (std::signbit(ray.direction.y)
				? -math::zero_div_epsilon
				:  math::zero_div_epsilon)
			: ray.direction.y
		),

		1.0f / (fabsf(ray.direction.z) < math::zero_div_epsilon
			? (std::signbit(ray.direction.z)
				? -math::zero_div_epsilon
				:  math::zero_div_epsilon)
			: ray.direction.z
		)
	};

	const __m256 _x_rayorig = _mm256_set1_ps(ray.origin.x);
	const __m256 _y_rayorig = _mm256_set1_ps(ray.origin.y);
	const __m256 _z_rayorig = _mm256_set1_ps(ray.origin.z);
	const __m256 _x_dirinv  = _mm256_set1_ps(dir_inv.x);
	const __m256 _y_dirinv  = _mm256_set1_ps(dir_inv.y);
	const __m256 _z_dirinv  = _mm256_set1_ps(dir_inv.z);
	const __m256 _zero      = _mm256_setzero_ps();

	struct TravStackNode
	{
		uint32_t idx   {0};
		uint32_t depth {0};
	};

	TravStackNode tlas_trav_stack[cfg::bvh_max_depth];
	uint32_t tlas_stack_ptr = 0;
	
	tlas_trav_stack[tlas_stack_ptr++] = {spatial_rig.tlas_root_idx, 0};

	const rdr::SceneVertex* vtx_data = staging_ctx.scn_vtx_mass->vtx_data();
	const uint32_t*         idx_data = staging_ctx.scn_vtx_mass->idx_data();

	/* tlas traversal */

	while (tlas_stack_ptr > 0) {

		TravStackNode tlas_stack_node = tlas_trav_stack[--tlas_stack_ptr];
		const auto& tlbvh_node = spatial_rig.tlas_nodes[tlas_stack_node.idx];

		/* x axis intersect */

		__m256 _x_tl_dist_0 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.x_min),
				_x_rayorig
			),
			_x_dirinv
		);
		__m256 _x_tl_dist_1 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.x_max),
				_x_rayorig
			),
			_x_dirinv
		);
		__m256 _x_tl_dist_min = _mm256_min_ps(_x_tl_dist_0, _x_tl_dist_1);
		__m256 _x_tl_dist_max = _mm256_max_ps(_x_tl_dist_0, _x_tl_dist_1);

		/* y axis intersect */

		__m256 _y_tl_dist_0 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.y_min),
				_y_rayorig
			),
			_y_dirinv
		);
		__m256 _y_tl_dist_1 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.y_max),
				_y_rayorig
			),
			_y_dirinv
		);
		__m256 _y_tl_dist_min = _mm256_min_ps(_y_tl_dist_0, _y_tl_dist_1);
		__m256 _y_tl_dist_max = _mm256_max_ps(_y_tl_dist_0, _y_tl_dist_1);

		/* z axis intersect */

		__m256 _z_tl_dist_0 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.z_min),
				_z_rayorig
			),
			_z_dirinv
		);
		__m256 _z_tl_dist_1 = _mm256_mul_ps(
			_mm256_sub_ps(
				_mm256_loadu_ps(tlbvh_node.z_max),
				_z_rayorig
			),
			_z_dirinv
		);
		__m256 _z_tl_dist_min = _mm256_min_ps(_z_tl_dist_0, _z_tl_dist_1);
		__m256 _z_tl_dist_max = _mm256_max_ps(_z_tl_dist_0, _z_tl_dist_1);

		/* tlas traversal: aabb intersection test & lanes hit mask */

		__m256 _tl_dist_entry = _mm256_max_ps(
			_mm256_max_ps(_x_tl_dist_min, _y_tl_dist_min),
			_mm256_max_ps(_z_tl_dist_min, _zero)
		);
		__m256 _tl_dist_exit = _mm256_min_ps(
			_mm256_min_ps(_x_tl_dist_max, _y_tl_dist_max),
			_mm256_min_ps(
				_z_tl_dist_max,
				_mm256_set1_ps(closest_hit.closest_hit_dist)
			)
		);

		uint32_t tl_active_lanes = static_cast<uint32_t>(
			_mm256_movemask_ps(
				_mm256_cmp_ps(_tl_dist_entry, _tl_dist_exit, _CMP_LE_OQ)
			)
		);

		/* tlas traversal: iterate lanes hit mask and process node */

		while (tl_active_lanes) {
			uint32_t tl_lane =
				static_cast<uint32_t>(std::countr_zero(tl_active_lanes));
			tl_active_lanes &= (tl_active_lanes - 1);

			if (tlbvh_node.x_min[tl_lane] == std::numeric_limits<float>::max()) {
				continue;
			}

			if (tlbvh_node.blas_count[tl_lane] == 0) {
				tlas_trav_stack[tlas_stack_ptr++] = {
					tlbvh_node.blas_first[tl_lane],
					tlas_stack_node.depth + 1
				};
				continue;
			}

			/* tlas leaf: extract blas range and traverse blas */

			uint32_t blas_first = tlbvh_node.blas_first[tl_lane];
			uint32_t blas_count = tlbvh_node.blas_count[tl_lane];

			for (uint32_t blas_counter = 0; blas_counter < blas_count; ++blas_counter) {

				uint32_t submesh_idx = spatial_rig.tlas_sbm_leaves[blas_first + blas_counter];
				uint32_t blas_root   = spatial_rig.blas_sbm_roots[submesh_idx];

				if (blas_root == 0xFFFFFFFFU) {
					continue;
				}

				mat4 mtx_M     = rdr_rig.stat_mtxs_M[submesh_idx];
				mat4 mtx_M_inv = glm::inverse(mtx_M);

				vec3 ray_ori_L = vec3(mtx_M_inv * vec4(ray.origin, 1.0f));
				vec3 ray_dir_L = vec3(mtx_M_inv * vec4(ray.direction, 0.0f));
				
				vec3 ray_dir_L_inv = vec3(

					1.0f / (fabsf(ray_dir_L.x) < math::zero_div_epsilon
						? (std::signbit(ray_dir_L.x)
							? -math::zero_div_epsilon
							:  math::zero_div_epsilon)
						: ray_dir_L.x
					),

					1.0f / (fabsf(ray_dir_L.y) < math::zero_div_epsilon
						? (std::signbit(ray_dir_L.y)
							? -math::zero_div_epsilon
							:  math::zero_div_epsilon)
						: ray_dir_L.y
					),

					1.0f / (fabsf(ray_dir_L.z) < math::zero_div_epsilon
						? (std::signbit(ray_dir_L.z)
							? -math::zero_div_epsilon
							:  math::zero_div_epsilon)
						: ray_dir_L.z
					)
				);

				/* blas traversal */

				uint32_t sbm_tested_node_idxs[cfg::bvh_max_depth];
				uint32_t sbm_tested_node_cnt = 0;
				uint32_t sbm_path_node_idxs[cfg::bvh_max_depth];

				TravStackNode blas_trav_stack[cfg::bvh_max_depth];
				uint32_t blas_stack_ptr = 0;

				blas_trav_stack[blas_stack_ptr++] = {blas_root, 0};

				while (blas_stack_ptr > 0) {

					TravStackNode blas_stack_node = blas_trav_stack[--blas_stack_ptr];
					uint32_t blas_stack_idx       = blas_stack_node.idx;
					uint32_t blas_stack_depth     = blas_stack_node.depth;

					if (blas_stack_depth < cfg::bvh_max_depth) {
						sbm_path_node_idxs[blas_stack_depth] = blas_stack_idx;
					}

					const auto& blas_node = spatial_rig.blas_nodes[blas_stack_idx];

					/* blas traversal: decode quantized grid space */

					vec3 grid_scale_factor    = (blas_node.max - blas_node.min) * (1.0f / 255.0f);
					vec3 ray_dir_L_inv_scaled = grid_scale_factor * ray_dir_L_inv;
					vec3 ray_ori_L_dist       = (blas_node.min - ray_ori_L) * ray_dir_L_inv;

					__m256 _x_dirinv_L = _mm256_set1_ps(ray_dir_L_inv_scaled.x);
					__m256 _y_dirinv_L = _mm256_set1_ps(ray_dir_L_inv_scaled.y);
					__m256 _z_dirinv_L = _mm256_set1_ps(ray_dir_L_inv_scaled.z);

					__m256 _x_oridist_L = _mm256_set1_ps(ray_ori_L_dist.x);
					__m256 _y_oridist_L = _mm256_set1_ps(ray_ori_L_dist.y);
					__m256 _z_oridist_L = _mm256_set1_ps(ray_ori_L_dist.z);

					/* x axis intersect */

					__m256 _x_bl_dist_0 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.x_min),
						_x_dirinv_L,
						_x_oridist_L
					);
					__m256 _x_bl_dist_1 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.x_max),
						_x_dirinv_L,
						_x_oridist_L
					);
					__m256 _x_bl_dist_min = _mm256_min_ps(_x_bl_dist_0, _x_bl_dist_1);
					__m256 _x_bl_dist_max = _mm256_max_ps(_x_bl_dist_0, _x_bl_dist_1);

					/* y axis intersect */

					__m256 _y_bl_dist_0 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.y_min),
						_y_dirinv_L,
						_y_oridist_L
					);
					__m256 _y_bl_dist_1 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.y_max),
						_y_dirinv_L,
						_y_oridist_L
					);
					__m256 _y_bl_dist_min = _mm256_min_ps(_y_bl_dist_0, _y_bl_dist_1);
					__m256 _y_bl_dist_max = _mm256_max_ps(_y_bl_dist_0, _y_bl_dist_1);

					/* z axis intersect */

					__m256 _z_bl_dist_0 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.z_min),
						_z_dirinv_L,
						_z_oridist_L
					);
					__m256 _z_bl_dist_1 = _mm256_fmadd_ps(
						load_u8_to_ps256(blas_node.z_max),
						_z_dirinv_L,
						_z_oridist_L
					);
					__m256 _z_bl_dist_min = _mm256_min_ps(_z_bl_dist_0, _z_bl_dist_1);
					__m256 _z_bl_dist_max = _mm256_max_ps(_z_bl_dist_0, _z_bl_dist_1);

					/* blas traversal: aabb intersection test & lanes hit mask */

					__m256 _bl_dist_entry = _mm256_max_ps(
						_mm256_max_ps(_x_bl_dist_min, _y_bl_dist_min),
						_mm256_max_ps(_z_bl_dist_min, _zero)
					);
					__m256 _bl_dist_exit  = _mm256_min_ps(
						_mm256_min_ps(_x_bl_dist_max, _y_bl_dist_max),
						_mm256_min_ps(
							_z_bl_dist_max,
							_mm256_set1_ps(closest_hit.closest_hit_dist)
						)
					);

					uint32_t bl_active_lanes = static_cast<uint32_t>(
						_mm256_movemask_ps(
							_mm256_cmp_ps(_bl_dist_entry, _bl_dist_exit, _CMP_LE_OQ)
						)
					);

					/* extract lane hits & filter null lanes */

					alignas(32) float dist_entries[8];
					_mm256_store_ps(dist_entries, _bl_dist_entry);

					struct BlasLaneHit
					{
						uint32_t lane    {0};
						float entry_dist {0.0f};
					};

					BlasLaneHit lane_hits[8];
					uint32_t hit_cnt = 0;

					while (bl_active_lanes) {
						uint32_t bl_lane =
							static_cast<uint32_t>(std::countr_zero(bl_active_lanes));
						bl_active_lanes &= (bl_active_lanes - 1);

						if (blas_node.x_min[bl_lane] != geo::cfg::blas_null_lane) {
							lane_hits[hit_cnt++] = {
								bl_lane,
								dist_entries[bl_lane]
							};
						}
					}

					/* blas traversal: sort hits ascending by distance */

					for (uint32_t curr_idx = 1; curr_idx < hit_cnt; ++curr_idx) {

						BlasLaneHit hit_insert = lane_hits[curr_idx];
						int32_t prev_idx = curr_idx - 1;

						while (
							prev_idx >= 0 &&
							lane_hits[prev_idx].entry_dist > hit_insert.entry_dist
						) {
							lane_hits[prev_idx + 1] = lane_hits[prev_idx];
							--prev_idx;
						}

						lane_hits[prev_idx + 1] = hit_insert;
					}

					/* blas traversal: leaves */

					for (uint32_t hit_idx = 0; hit_idx < hit_cnt; ++hit_idx) {
						uint32_t bl_lane = lane_hits[hit_idx].lane;
						
						if (lane_hits[hit_idx].entry_dist >= closest_hit.closest_hit_dist) {
							break;
						}

						if (blas_node.tris_cnts[bl_lane] > 0) {

							const auto& submesh = rdr_rig.stat_submeshes[submesh_idx];
							uint32_t tri_idx = blas_node.base_idxs[bl_lane] - submesh.idx_first / 3;

							uint32_t idx_0 = idx_data[submesh.idx_first + (tri_idx * 3) + 0];
							uint32_t idx_1 = idx_data[submesh.idx_first + (tri_idx * 3) + 1];
							uint32_t idx_2 = idx_data[submesh.idx_first + (tri_idx * 3) + 2];

							const auto& vtx_0_raw = vtx_data[submesh.vtx_base + idx_0];
							const auto& vtx_1_raw = vtx_data[submesh.vtx_base + idx_1];
							const auto& vtx_2_raw = vtx_data[submesh.vtx_base + idx_2];

							vec3 vtx_0(vtx_0_raw.pos.x, vtx_0_raw.pos.y, vtx_0_raw.pos.z);
							vec3 vtx_1(vtx_1_raw.pos.x, vtx_1_raw.pos.y, vtx_1_raw.pos.z);
							vec3 vtx_2(vtx_2_raw.pos.x, vtx_2_raw.pos.y, vtx_2_raw.pos.z);

							float u_bary, v_bary, dist_hit_L;
							if (scn::intersect_ray_triangle(
									ray_ori_L,
									ray_dir_L,
									vtx_0,
									vtx_1,
									vtx_2,
									dist_hit_L,
									u_bary,
									v_bary
								)
							) {
								vec3 pos_hit_L = ray_ori_L + ray_dir_L * dist_hit_L;
								vec3 pos_hit_W = vec3(mtx_M * vec4(pos_hit_L, 1.0f));
								float dist_W = glm::length(pos_hit_W - ray.origin);

								if (dist_W < closest_hit.closest_hit_dist) {
									closest_hit.is_hit = true;
									closest_hit.entity = rdr_rig.stat_entities[submesh_idx];
									closest_hit.submesh = submesh_idx;
									
									closest_hit.closest_hit_dist = dist_W;
									closest_hit.tested_node_cnt  = sbm_tested_node_cnt;
									closest_hit.path_node_cnt    = blas_stack_depth + 1;

									std::memcpy(
										closest_hit.tested_node_idxs,
										sbm_tested_node_idxs,
										sbm_tested_node_cnt * sizeof(uint32_t)
									);

									std::memcpy(
										closest_hit.path_node_idxs,
										sbm_path_node_idxs,
										(blas_stack_depth + 1) * sizeof(uint32_t)
									);
								}
							}
						}
					}

					/* blas traversal: push interior nodes in reverse order */

					for (int32_t hit_idx = static_cast<int32_t>(hit_cnt) - 1; hit_idx >= 0; --hit_idx) {
						uint32_t bl_lane = lane_hits[hit_idx].lane;

						if (lane_hits[hit_idx].entry_dist >= closest_hit.closest_hit_dist) {
							continue;
						}

						if (blas_node.tris_cnts[bl_lane] == 0) {
							if (sbm_tested_node_cnt < cfg::bvh_max_depth) {
								sbm_tested_node_idxs[sbm_tested_node_cnt++] = blas_node.base_idxs[bl_lane];
							}
							blas_trav_stack[blas_stack_ptr++] = {
								blas_node.base_idxs[bl_lane],
								blas_stack_depth + 1
							};
						}
					}
				}
			}
		}
	}

	return closest_hit;
}

} // hpr::scn

