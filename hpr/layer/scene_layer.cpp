#include "scene_layer.hpp"

#include "math.hpp"
#include "panic.hpp"
#include "event.hpp"
#include "stats.hpp"

#include "raycast.hpp"
#include "bvh_tlas.hpp"
#include "anim_data.hpp"
#include "scene_context.hpp"
#include "systems_scene.hpp"
#include "draw_view_data.hpp"
#include "draw_queue_data.hpp"

#include <bit>
#include <limits>
#include <chrono>
#include <immintrin.h>


namespace hpr::lyr {


SceneLayer::SceneLayer(
	scn::Scene                             scene,
	MainRegistry&                          ecs_registry,
	mtp::shared<mtp_scn_set>&              metapool,
	rdr::SurfaceInfo                       surface_info,
	rdr::StagingContext                    staging_ctx,
	geo::CanonicalShapes                   canonical_shapes,
	rdr::RenderQueue<rdr::SceneDrawCmd>&   scene_queue,
	rdr::RenderQueue<rdr::AnimDrawCmd>&    anim_queue,
	rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
	rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue,
	log::StatsHarvester&                   harvester
)
	: m_scene_rig     {std::move(scene)}
	, m_registry      {ecs_registry}
	, m_metapool      {metapool}
	, m_surface_info  {surface_info}
	, m_staging_ctx   {staging_ctx}
	, m_canonical     {canonical_shapes}
	, m_scene_queue   {scene_queue}
	, m_anim_queue    {anim_queue}
	, m_cue_queue     {cue_queue}
	, m_overlay_queue {overlay_queue}
	, m_harvester     {harvester}
{
	m_hiz_buffer.mips[0].resize(256 * 128);
	m_hiz_buffer.mips[1].resize(128 * 64);
	m_hiz_buffer.mips[2].resize(64  * 32);

	uint32_t worker_thread_count = std::thread::hardware_concurrency();

	if (worker_thread_count == 0) {
		worker_thread_count = 1;
	}
	else if (worker_thread_count > 1) {
		worker_thread_count -= 1;
	}

	if (worker_thread_count > 8) {
		worker_thread_count = 8;
	}

	m_job_scheduler.init(worker_thread_count);
}


void SceneLayer::on_attach()
{
	m_scene_rig.clear_volatile();

	ecs::TransformSystem::update(m_registry);
}


void SceneLayer::on_detach()
{}


bool SceneLayer::on_event(Event& event)
{
	return false;
}


bool SceneLayer::on_actions(
	const scn::SceneContext&     scene_ctx,
	std::span<const io::Action>  actions
)
{
	bool action_consumed = false;

	for (const io::Action& action : actions) {
		switch (action.kind) {

		case io::ActionKind::SelectClick:
		{
			const auto& payload = std::get<io::SelectClickAction>(action.payload);

			m_pick_ctx.ray = scn::make_pick_ray(
				payload.x,
				payload.y,
				m_surface_info.width,
				m_surface_info.height,
				scene_ctx.draw_view
			);

			m_pick_ctx.is_pending = true;
			action_consumed       = true;

			break;
		}

		case io::ActionKind::DebugToggleBVH:
		{
			m_show_bvh = !m_show_bvh;
			break;
		}
		case io::ActionKind::DebugToggleCulling:
		{
			m_show_cull = !m_show_cull;
			break;
		}

		default: break;
		}
	}
	return action_consumed;
}


void SceneLayer::on_update(scn::SceneContext& scene_ctx, float delta_time)
{
	process_raycast();
	clear_and_resize();

	/* build scene ctx */

	auto& rdr_rig = m_scene_rig.render_rig;
	scene_ctx.light_set.ambient_rgb = rdr_rig.ambient_rgb;
	scene_ctx.light_set = ecs::LightSystem::build_light(
		m_registry,
		scene_ctx.draw_view
	);

	sync_static_models();
	sync_skinned_models(scene_ctx);
	sync_occluder_twins(scene_ctx);
	
	rasterize_hiz();
	dispatch_culling(scene_ctx);

	build_bvh_tlas();
}


void SceneLayer::on_submit(const scn::SceneContext& scene_ctx, uint32_t layer_idx)
{
	auto& rdr_rig = m_scene_rig.render_rig;

	const uint32_t total_prims = static_cast<uint32_t>(rdr_rig.stat_submeshes.size());

	HPR_ASSERT(total_prims < cfg::max_scene_prims);
	if (total_prims == 0) {
		return;
	}

	submit_static_geometry(layer_idx);
	submit_skinned_geometry(layer_idx);
	submit_debug_wires(layer_idx);
}


void SceneLayer::process_raycast()
{
	if (m_pick_ctx.is_pending) {
		auto raycast_start = std::chrono::high_resolution_clock::now();

		const scn::RayHit ray_hit = raycast_scene(
			m_pick_ctx.ray,
			m_scene_rig,
			m_staging_ctx
		);

		auto raycast_end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> raycast_ms = raycast_end - raycast_start;
		m_harvester.scene_layer_curr().raycast_ms = raycast_ms.count();

		m_pick_ctx.is_pending = false;

		if (ray_hit.is_hit) {
			m_selection.entity = ray_hit.entity;
			const auto* trs_comp = m_registry.get<ecs::TransformComponent>(m_selection.entity);
			if (trs_comp) {
				m_selection.transform.position = trs_comp->position;
				m_selection.transform.rotation = trs_comp->rotation;
				m_selection.transform.scale    = trs_comp->scale;
			}
			m_selection.submesh = ray_hit.submesh;

			m_pick_ctx.tested_node_cnt = ray_hit.tested_node_cnt;
			m_pick_ctx.path_node_cnt   = ray_hit.path_node_cnt;

			std::memcpy(
				m_pick_ctx.tested_node_idxs,
				ray_hit.tested_node_idxs,
				m_pick_ctx.tested_node_cnt * sizeof(uint32_t)
			);
			std::memcpy(
				m_pick_ctx.path_node_idxs,
				ray_hit.path_node_idxs,
				m_pick_ctx.path_node_cnt * sizeof(uint32_t)
			);
		}
		else {
			m_selection.transform = {};
			m_selection.entity    = ecs::ctx::invalid_entity;
			m_selection.submesh   = std::numeric_limits<uint32_t>::max();

			m_pick_ctx.tested_node_cnt = 0;
			m_pick_ctx.path_node_cnt   = 0;
		}

		auto* event = m_event_queue->push<SelectionChangedEvent>();
		event->selection.transform = m_selection.transform;
		event->selection.entity    = m_selection.entity;
		event->selection.submesh   = m_selection.submesh;
		event->emitter             = this;
	}
}


void SceneLayer::clear_and_resize()
{
	auto& rdr_rig     = m_scene_rig.render_rig;
	auto& cull_rig    = m_scene_rig.cull_rig;
	auto& spatial_rig = m_scene_rig.spatial_rig;

	/* clear last frame & resize */

	m_scene_rig.clear_volatile();

	spatial_rig.tlas_nodes.clear();

	m_staging_ctx.scn_blob_mass->clear();
	m_staging_ctx.anm_blob_mass->clear();
	m_staging_ctx.cue_blob_mass->clear();
	m_staging_ctx.orl_blob_mass->clear();

	const size_t stat_sbms_total = rdr_rig.stat_submeshes.size();
	rdr_rig.resize_aabb_world(stat_sbms_total);
	rdr_rig.stat_ecs_blob_idxs.resize(stat_sbms_total);

	const size_t skin_sbms_total = rdr_rig.skin_submeshes.size();
	rdr_rig.skin_ecs_blob_idxs.resize(skin_sbms_total);

	uint32_t bones_total = 0;
	auto& anim_rig = m_scene_rig.anim_rig;

	m_registry.template scan<ecs::AnimComponent>(
		[&anim_rig, &bones_total](ecs::Entity entity, const ecs::AnimComponent& anim_comp)
		{
			bones_total += anim_rig.skeletons[anim_comp.skeleton_idx].bone_count;
		}
	);

	anim_rig.mtxs_M_bones.resize(bones_total);

	const size_t occluders_total = m_registry.template size<ecs::OccluderComponent>();
	cull_rig.mtxs_MVP_occluder.reserve(occluders_total);
	cull_rig.occluder_idxs.reserve(occluders_total);

	for (auto& mip : m_hiz_buffer.mips) {
		std::fill(mip.begin(), mip.end(), 1.0f);
	}
}


void SceneLayer::sync_static_models()
{
	auto& rdr_rig        = m_scene_rig.render_rig;
	auto& model_trs_mass = m_staging_ctx.scn_blob_mass;
	
	m_registry.template scan<ecs::ModelComponent, ecs::TransformComponent>(
		[&rdr_rig, &model_trs_mass](
			ecs::Entity                    entity,
			const ecs::ModelComponent&     model,
			const ecs::TransformComponent& transform
		)
		{
			const mat4 mtx_W     = transform.mtx_W;
			const mat3 mtx_WN    = glm::transpose(glm::inverse(mat3(mtx_W)));
			const mat3 mtx_W_abs = mat3(
				glm::abs(mtx_W[0]),
				glm::abs(mtx_W[1]),
				glm::abs(mtx_W[2])
			);

			for (uint32_t sbm_idx_loc = 0; sbm_idx_loc < model.sbm_count; ++sbm_idx_loc) {
				const uint32_t sbm_idx_glob = model.sbm_first + sbm_idx_loc;

				const mat4 mtx_M =      mtx_W  * rdr_rig.stat_mtxs_L[sbm_idx_glob];
				const mat4 mtx_N = mat4(mtx_WN * rdr_rig.stat_mtxs_LN[sbm_idx_glob]);

				rdr_rig.stat_mtxs_M[sbm_idx_glob] = mtx_M;

				rdr_rig.stat_ecs_blob_idxs[sbm_idx_glob] =
					model_trs_mass->push_raw({mtx_M, mtx_N, rdr_rig.stat_material_idxs[sbm_idx_glob]});

				const scn::AABB& aabb_L = rdr_rig.stat_aabb_L[sbm_idx_glob];

				const vec3 center_L = (aabb_L.max + aabb_L.min) * 0.5f;
				const vec3 half_L   = (aabb_L.max - aabb_L.min) * 0.5f;

				const vec3 center_W = vec3(mtx_W * vec4(center_L, 1.0f));
				const vec3 half_W   = mtx_W_abs * half_L;

				const vec3 pos_min = center_W - half_W;
				const vec3 pos_max = center_W + half_W;

				rdr_rig.stat_aabb_x_min_W[sbm_idx_glob] = pos_min.x;
				rdr_rig.stat_aabb_y_min_W[sbm_idx_glob] = pos_min.y;
				rdr_rig.stat_aabb_z_min_W[sbm_idx_glob] = pos_min.z;
				rdr_rig.stat_aabb_x_max_W[sbm_idx_glob] = pos_max.x;
				rdr_rig.stat_aabb_y_max_W[sbm_idx_glob] = pos_max.y;
				rdr_rig.stat_aabb_z_max_W[sbm_idx_glob] = pos_max.z;
			}
		}
	);
}


void SceneLayer::sync_skinned_models(const scn::SceneContext& scene_ctx)
{
	auto& rdr_rig        = m_scene_rig.render_rig;
	auto& anim_rig       = m_scene_rig.anim_rig;
	auto& anim_blob_mass = m_staging_ctx.anm_blob_mass;
	
	m_registry.template scan<ecs::ModelComponent, ecs::AnimComponent, ecs::TransformComponent>(
		[&rdr_rig, &anim_rig, &anim_blob_mass, &scene_ctx](
			ecs::Entity                    entity,
			const ecs::ModelComponent&     model_comp,
			ecs::AnimComponent&            anim_comp,
			const ecs::TransformComponent& transform_comp
		)
		{
			anim_comp.local_time += scene_ctx.delta_time;
			const anm::Skeleton& skeleton = anim_rig.skeletons[anim_comp.skeleton_idx];
			const anm::AnimClip& clip     = anim_rig.clips[anim_comp.clip_idx];

			anim_comp.local_time = std::fmod(anim_comp.local_time, clip.duration_ticks);

			const uint32_t bone_count = skeleton.bone_count;
			anim_comp.base_pose_idx   = static_cast<uint32_t>(anim_rig.mtxs_M_bones.size());

			anim_rig.mtxs_M_bones.resize(anim_comp.base_pose_idx + bone_count);

			std::array<mat4, anm::cfg::max_skeleton_bones> mtxs_L_bones;

			std::memcpy(
				mtxs_L_bones.data(),
				&anim_rig.mtxs_L_rest[skeleton.rest_mtx_L_first],
				bone_count * sizeof(mat4)
			);

			for (uint32_t track_idx_loc = 0; track_idx_loc < clip.track_count; ++track_idx_loc) {

				const anm::AnimTrack& track = anim_rig.tracks[clip.track_first + track_idx_loc];

				if (track.bone_idx >= bone_count || track.key_count == 0) {
					continue;
				}

				vec3 tsl_L;
				quat rot_L;

				if (track.key_count == 1) {
					tsl_L = anim_rig.key_tsls[track.key_tsl_first];
					rot_L = anim_rig.key_rots[track.key_rot_first];
				}
				else {
					const float* times = &anim_rig.key_times[track.key_time_first];

					uint32_t key_idx_1 = 0;
					while (key_idx_1 < track.key_count && times[key_idx_1] <= anim_comp.local_time) {
						++key_idx_1;
					}

					if (key_idx_1 == 0) {
						tsl_L = anim_rig.key_tsls[track.key_tsl_first];
						rot_L = anim_rig.key_rots[track.key_rot_first];
					}
					else if (key_idx_1 == track.key_count) {
						tsl_L = anim_rig.key_tsls[track.key_tsl_first + track.key_count - 1];
						rot_L = anim_rig.key_rots[track.key_rot_first + track.key_count - 1];
					}
					else {
						const uint32_t key_idx_0 = key_idx_1 - 1;
						const float time_0       = times[key_idx_0];
						const float time_1       = times[key_idx_1];
						const float blend_weight = (anim_comp.local_time - time_0) / (time_1 - time_0);

						const auto& tsl_0 = anim_rig.key_tsls[track.key_tsl_first + key_idx_0];
						const auto& tsl_1 = anim_rig.key_tsls[track.key_tsl_first + key_idx_1];

						tsl_L = glm::mix(tsl_0, tsl_1, blend_weight);

						const auto& rot_0 = anim_rig.key_rots[track.key_rot_first + key_idx_0];
						const auto& rot_1 = anim_rig.key_rots[track.key_rot_first + key_idx_1];

						rot_L = glm::slerp(rot_0, rot_1, blend_weight);
					}
				}

				mtxs_L_bones[track.bone_idx] =
					glm::translate(mat4(1.0f), tsl_L) * glm::mat4_cast(rot_L);
			}

			for (uint32_t bone_idx_loc = 0; bone_idx_loc < bone_count; ++bone_idx_loc) {
				int16_t parent_idx_loc = anim_rig.parent_idxs[skeleton.parent_idx_first + bone_idx_loc];
				uint32_t bone_idx_glob = anim_comp.base_pose_idx + bone_idx_loc;

				if (parent_idx_loc == -1) {
					anim_rig.mtxs_M_bones[bone_idx_glob] = mtxs_L_bones[bone_idx_loc];
				}
				else {
					uint32_t parent_idx_glob = anim_comp.base_pose_idx + parent_idx_loc;
					anim_rig.mtxs_M_bones[bone_idx_glob] =
						anim_rig.mtxs_M_bones[parent_idx_glob] * mtxs_L_bones[bone_idx_loc];
				}

				anim_rig.mtxs_M_bones[bone_idx_glob] =
					anim_rig.mtxs_M_bones[bone_idx_glob] *
						anim_rig.mtxs_inv_bind[skeleton.bind_mtx_inv_first + bone_idx_loc];
			}

			const mat4 mtx_W  = transform_comp.mtx_W;
			const mat3 mtx_WN = glm::transpose(glm::inverse(mat3(mtx_W)));

			for (uint32_t sbm_idx_loc = 0; sbm_idx_loc < model_comp.sbm_count; ++sbm_idx_loc) {
				const uint32_t sbm_idx_glob = model_comp.sbm_first + sbm_idx_loc;
				const mat4 mtx_M = mtx_W * rdr_rig.skin_mtxs_L[sbm_idx_glob];
				const mat4 mtx_N = mat4(mtx_WN * rdr_rig.skin_mtxs_LN[sbm_idx_glob]);
				const uint32_t material_idx = rdr_rig.skin_material_idxs[sbm_idx_glob];

				rdr_rig.skin_ecs_blob_idxs[sbm_idx_glob] = anim_blob_mass->push_raw({
					mtx_M,
					mtx_N,
					anim_comp.base_pose_idx,
					material_idx
				});
			}
		}
	);
}


void SceneLayer::sync_occluder_twins(const scn::SceneContext& scene_ctx)
{
	auto& cull_rig = m_scene_rig.cull_rig;

	m_registry.template scan<ecs::OccluderComponent, ecs::TransformComponent>(
		[&cull_rig, &scene_ctx](
			ecs::Entity                    entity,
			const ecs::OccluderComponent&  occluder,
			const ecs::TransformComponent& transform
		)
		{
			const mat4 mtx_M   = transform.mtx_W * occluder.mtx_L;
			const mat4 mtx_MVP = scene_ctx.draw_view.mtx_VP * mtx_M;

			cull_rig.mtxs_MVP_occluder.push_back(mtx_MVP);
			cull_rig.occluder_idxs.push_back(occluder.twin_idx);
		}
	);
}


void SceneLayer::rasterize_hiz()
{
	auto& cull_rig = m_scene_rig.cull_rig;

	/* hiz depth prepass: rasterize low poly twins */

	auto raster_start = std::chrono::high_resolution_clock::now();

	const float hiz_width  = static_cast<float>(HiZBuffer::width);
	const float hiz_height = static_cast<float>(HiZBuffer::height);

	const uint32_t total_occluders_vis = static_cast<uint32_t>(cull_rig.occluder_idxs.size());

	for (uint32_t occr_idx = 0; occr_idx < total_occluders_vis; ++occr_idx) {

		const uint32_t twin_idx = cull_rig.occluder_idxs[occr_idx];
		const auto& twin_slice  = cull_rig.twin_geoslices[twin_idx];

		HPR_ASSERT_MSG(twin_slice.is_valid(),
			"occluder twin geometry pipeline fail");

		const mat4 mtx_MVP = cull_rig.mtxs_MVP_occluder[occr_idx];

		for (uint32_t twin_slice_idx = 0; twin_slice_idx < twin_slice.idx_count; twin_slice_idx += 3) {

			uint32_t idx_0 = cull_rig.twin_indices[twin_slice.idx_first + twin_slice_idx + 0];
			uint32_t idx_1 = cull_rig.twin_indices[twin_slice.idx_first + twin_slice_idx + 1];
			uint32_t idx_2 = cull_rig.twin_indices[twin_slice.idx_first + twin_slice_idx + 2];

			vec4 vtx_0_clip = mtx_MVP * vec4(cull_rig.twin_positions[twin_slice.vtx_base + idx_0], 1.0f);
			vec4 vtx_1_clip = mtx_MVP * vec4(cull_rig.twin_positions[twin_slice.vtx_base + idx_1], 1.0f);
			vec4 vtx_2_clip = mtx_MVP * vec4(cull_rig.twin_positions[twin_slice.vtx_base + idx_2], 1.0f);

			if (vtx_0_clip.w <= 0.0f || vtx_1_clip.w <= 0.0f || vtx_2_clip.w <= 0.0f) {
				continue;
			}

			vec3 vtx_0_ndc = vec3(vtx_0_clip) / vtx_0_clip.w;
			vec3 vtx_1_ndc = vec3(vtx_1_clip) / vtx_1_clip.w;
			vec3 vtx_2_ndc = vec3(vtx_2_clip) / vtx_2_clip.w;

			vtx_0_ndc.z = vtx_0_ndc.z * 0.5f + 0.5f;
			vtx_1_ndc.z = vtx_1_ndc.z * 0.5f + 0.5f;
			vtx_2_ndc.z = vtx_2_ndc.z * 0.5f + 0.5f;

			vec2 vtx_0_px = (vec2(vtx_0_ndc) * 0.5f + 0.5f) * vec2(hiz_width, hiz_height);
			vec2 vtx_1_px = (vec2(vtx_1_ndc) * 0.5f + 0.5f) * vec2(hiz_width, hiz_height);
			vec2 vtx_2_px = (vec2(vtx_2_ndc) * 0.5f + 0.5f) * vec2(hiz_width, hiz_height);

			int32_t x_min = std::clamp(
				static_cast<int32_t>(std::min({vtx_0_px.x, vtx_1_px.x, vtx_2_px.x})),
				0,
				static_cast<int32_t>(HiZBuffer::width  - 1)
			);
			int32_t y_min = std::clamp(
				static_cast<int32_t>(std::min({vtx_0_px.y, vtx_1_px.y, vtx_2_px.y})),
				0,
				static_cast<int32_t>(HiZBuffer::height - 1)
			);
			int32_t x_max = std::clamp(
				static_cast<int32_t>(std::max({vtx_0_px.x, vtx_1_px.x, vtx_2_px.x})),
				0,
				static_cast<int32_t>(HiZBuffer::width  - 1)
			);
			int32_t y_max = std::clamp(
				static_cast<int32_t>(std::max({vtx_0_px.y, vtx_1_px.y, vtx_2_px.y})),
				0,
				static_cast<int32_t>(HiZBuffer::height - 1)
			);

			auto edge_func = [](const vec2& a, const vec2& b, const vec2& c) {
				return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
			};

			float area = edge_func(vtx_0_px, vtx_1_px, vtx_2_px);
			if (area <= 0.0f) {
				continue;
			}

			for (int32_t y = y_min; y <= y_max; ++y) {
				int32_t row_offset = y * static_cast<int32_t>(HiZBuffer::width);
				for (int32_t x = x_min; x <= x_max; ++x) {
					vec2 pos_px(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);

					float bary_weight_0 = edge_func(vtx_1_px, vtx_2_px, pos_px);
					float bary_weight_1 = edge_func(vtx_2_px, vtx_0_px, pos_px);
					float bary_weight_2 = edge_func(vtx_0_px, vtx_1_px, pos_px);

					if (bary_weight_0 >= 0.0f && bary_weight_1 >= 0.0f && bary_weight_2 >= 0.0f) {
						bary_weight_0 /= area;
						bary_weight_1 /= area;
						bary_weight_2 /= area;

						float z =
							bary_weight_0 * vtx_0_ndc.z +
							bary_weight_1 * vtx_1_ndc.z +
							bary_weight_2 * vtx_2_ndc.z;

						int32_t px_idx = row_offset + x;
						m_hiz_buffer.mips[0][static_cast<size_t>(px_idx)] =
							std::min(m_hiz_buffer.mips[0][static_cast<size_t>(px_idx)], z);
					}
				}
			}
		}
	}

	auto raster_end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> raster_ms = raster_end - raster_start;
	m_harvester.scene_layer_curr().hiz_raster_ms = raster_ms.count();

	/* hiz mip chain */

	for (int32_t mip_idx = 1; mip_idx < static_cast<int32_t>(cfg::hiz_mips_num); ++mip_idx) {
		const int32_t w_mip_src = static_cast<int32_t>(HiZBuffer::width)  >> (mip_idx - 1);
		const int32_t h_mip_src = static_cast<int32_t>(HiZBuffer::height) >> (mip_idx - 1);

		const int32_t w_mip_dst = w_mip_src >> 1;
		const int32_t h_mip_dst = h_mip_src >> 1;

		const float* mip_src = m_hiz_buffer.mips[mip_idx - 1].data();
		float* mip_dst       = m_hiz_buffer.mips[mip_idx].data();

		for (int32_t y = 0; y < h_mip_dst; ++y) {
			for (int32_t x = 0; x < w_mip_dst; ++x) {
				const int32_t src_idx = (y * 2 * w_mip_src) + (x * 2);
				const float z_00 = mip_src[src_idx];
				const float z_01 = mip_src[src_idx + 1];
				const float z_10 = mip_src[src_idx + w_mip_src];
				const float z_11 = mip_src[src_idx + w_mip_src + 1];

				mip_dst[y * w_mip_dst + x] =
					std::max(std::max(z_00, z_01), std::max(z_10, z_11));
			}
		}
	}
}


void SceneLayer::dispatch_culling(const scn::SceneContext& scene_ctx)
{
	auto& rdr_rig  = m_scene_rig.render_rig;
	auto& cull_rig = m_scene_rig.cull_rig;

	/* frustum extract */

	std::array<FrustumPlane, math::frustum_plane_cnt> frustum_planes;
	for (size_t i = 0; i < math::frustum_plane_cnt; ++i) {
		const vec4 plane_raw = scene_ctx.draw_view.frustum[i];
		const vec3 nrm_raw {plane_raw.x, plane_raw.y, plane_raw.z};

		const float nrm_len     = glm::length(nrm_raw);
		const float nrm_len_inv = (nrm_len > 0.0f) ? (1.0f / nrm_len) : 0.0f;
		const vec3  nrm_unit    = nrm_raw * nrm_len_inv;

		frustum_planes[i] = {
			nrm_unit,
			glm::abs(nrm_unit),
			plane_raw.w * nrm_len_inv
		};
	}

	/* culling job setup */
	
	const size_t stat_sbms_total = rdr_rig.stat_submeshes.size();

	const uint32_t slice_count =
		(stat_sbms_total + cfg::job_grain - 1) / cfg::job_grain;

	mtp::slag<CullJobSlice, mtp::default_set> cull_slices(slice_count, CullJobSlice {});
	mtp::slag<CullAsyncResult, mtp_scn_set> cull_async_results {m_metapool};
	cull_async_results.resize(slice_count);

	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		auto& cull_slice = cull_slices[slice_idx];

		cull_slice.begin = slice_idx * cfg::job_grain;
		cull_slice.end   = std::min(cull_slice.begin + cfg::job_grain, static_cast<uint32_t>(stat_sbms_total));

		cull_slice.mtxs_M    = rdr_rig.stat_mtxs_M.data();

		cull_slice.aabb_min_x = rdr_rig.stat_aabb_x_min_W.data();
		cull_slice.aabb_min_y = rdr_rig.stat_aabb_y_min_W.data();
		cull_slice.aabb_min_z = rdr_rig.stat_aabb_z_min_W.data();
		cull_slice.aabb_max_x = rdr_rig.stat_aabb_x_max_W.data();
		cull_slice.aabb_max_y = rdr_rig.stat_aabb_y_max_W.data();
		cull_slice.aabb_max_z = rdr_rig.stat_aabb_z_max_W.data();

		cull_slice.frustum_planes = frustum_planes.data();
		cull_slice.cull_rig       = &cull_rig;
		cull_slice.occludee_idxs  = rdr_rig.stat_occludee_idxs.data();

		cull_slice.mtx_VP     = scene_ctx.draw_view.mtx_VP;
		cull_slice.hiz_buffer = &m_hiz_buffer;

		cull_slice.async_result = &cull_async_results[slice_idx];
		cull_slice.async_result->clear();
	}

	/* culling dispatch & wait */

	auto cull_start_time = std::chrono::steady_clock::now();

	job::JobLatch job_latch;
	m_job_scheduler.dispatch_range(
		job_latch,
		&SceneLayer::cull,
		slice_count,
		1,
		cull_slices.data()
	);

	job_latch.wait();

	auto cull_end_time = std::chrono::steady_clock::now();
	auto& scene_stats  = m_harvester.scene_layer_curr();
	
	scene_stats.cull_job_ms =
		std::chrono::duration<double, std::milli>(cull_end_time - cull_start_time).count();

	/* consolidate visible submeshes */

	size_t sbms_vis_total = 0;
	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		sbms_vis_total += cull_async_results[slice_idx].count;
	}

	rdr_rig.stat_sbms_visible.reserve(sbms_vis_total);

	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		auto& async_result = cull_async_results[slice_idx];

		scene_stats.frust_tested += async_result.frustum_tested;
		scene_stats.frust_culled += async_result.frustum_culled;
		scene_stats.occl_tested  += async_result.occlusion_tested;
		scene_stats.occl_culled  += async_result.occlusion_culled;

		for (uint32_t i = 0; i < async_result.count; ++i) {
			rdr_rig.stat_sbms_visible.push_back(async_result.sbms_visible[i]);
		}
	}
}


void SceneLayer::build_bvh_tlas()
{
	auto& rdr_rig     = m_scene_rig.render_rig;
	auto& spatial_rig = m_scene_rig.spatial_rig;

	const uint32_t sbms_vis_cnt =
		static_cast<uint32_t>(rdr_rig.stat_sbms_visible.size());

	if (sbms_vis_cnt > 0) {

		auto tlas_start = std::chrono::high_resolution_clock::now();

		mtp::vault<vec3,     mtp::default_set> model_mins;
		mtp::vault<vec3,     mtp::default_set> model_maxs;
		mtp::vault<uint32_t, mtp::default_set> model_sbm_offs;
		mtp::vault<uint32_t, mtp::default_set> model_sbm_cnts;

		spatial_rig.tlas_sbm_leaves.resize(sbms_vis_cnt);

		ecs::Entity curr_entity = ecs::ctx::invalid_entity;

		uint32_t curr_start_idx = 0;
		uint32_t curr_sbm_cnt   = 0;

		vec3 curr_min {std::numeric_limits<float>::max()};
		vec3 curr_max {std::numeric_limits<float>::lowest()};

		for (uint32_t vis_idx = 0; vis_idx < sbms_vis_cnt; ++vis_idx) {
			
			uint32_t sbm_idx   = rdr_rig.stat_sbms_visible[vis_idx];
			ecs::Entity entity = rdr_rig.stat_entities[sbm_idx];

			spatial_rig.tlas_sbm_leaves[vis_idx] = sbm_idx;

			if (entity != curr_entity) {
				if (curr_entity != ecs::ctx::invalid_entity) {
					model_mins.push_back(curr_min);
					model_maxs.push_back(curr_max);
					model_sbm_offs.push_back(curr_start_idx);
					model_sbm_cnts.push_back(curr_sbm_cnt);
				}

				curr_entity    = entity;
				curr_start_idx = vis_idx;
				curr_sbm_cnt = 0;
				curr_min       = vec3(std::numeric_limits<float>::max());
				curr_max       = vec3(std::numeric_limits<float>::lowest());
			}

			++curr_sbm_cnt;

			curr_min = glm::min(
				curr_min,
				vec3(
					rdr_rig.stat_aabb_x_min_W[sbm_idx],
					rdr_rig.stat_aabb_y_min_W[sbm_idx],
					rdr_rig.stat_aabb_z_min_W[sbm_idx]
			));
			curr_max = glm::max(
				curr_max,
				vec3(
					rdr_rig.stat_aabb_x_max_W[sbm_idx],
					rdr_rig.stat_aabb_y_max_W[sbm_idx],
					rdr_rig.stat_aabb_z_max_W[sbm_idx]
			));
		}

		if (curr_entity != ecs::ctx::invalid_entity) {
			model_mins.push_back(curr_min);
			model_maxs.push_back(curr_max);
			model_sbm_offs.push_back(curr_start_idx);
			model_sbm_cnts.push_back(curr_sbm_cnt);
		}

		geo::TlasBuilder tlas_builder;

		geo::TlasBVH tlas = tlas_builder.build(
			model_mins,
			model_maxs,
			model_sbm_offs,
			model_sbm_cnts
		);

		spatial_rig.tlas_nodes    = std::move(tlas.nodes);
		spatial_rig.tlas_root_idx = tlas.root_idx;

		auto tlas_end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> tlas_ms = tlas_end - tlas_start;
		m_harvester.scene_layer_curr().tlas_ms = tlas_ms.count();
	}
}


void SceneLayer::submit_static_geometry(uint32_t layer_idx)
{
	auto& rdr_rig         = m_scene_rig.render_rig;
	const auto& selection = m_selection.entity;
	
	for (uint32_t vis_idx = 0; vis_idx < rdr_rig.stat_sbms_visible.size(); ++vis_idx) {
		uint32_t sbm_idx_glob = rdr_rig.stat_sbms_visible[vis_idx];
		const auto& submesh   = rdr_rig.stat_submeshes[sbm_idx_glob];
		uint32_t material_idx = rdr_rig.stat_material_idxs[sbm_idx_glob];
		ecs::Entity entity    = rdr_rig.stat_entities[sbm_idx_glob];

		const uint64_t stat_sort_key =
			(static_cast<uint64_t>(material_idx      & 0xFFFFFFU) << 40) |
			(static_cast<uint64_t>(submesh.vtx_base  & 0xFFFFU)   << 24) |
			(static_cast<uint64_t>(submesh.idx_first & 0xFFFFFFU));

		const uint8_t stat_flags = (entity == selection)
			? static_cast<uint8_t>(rdr::SceneDrawCmdFlag::selected)
			: 0U;

		rdr::SceneDrawCmd cmd {
			.sort_key  = stat_sort_key,
			.layer_idx = layer_idx,
			.vtx_base  = submesh.vtx_base,
			.idx_first = submesh.idx_first,
			.idx_count = submesh.idx_count,
			.blob_idx  = rdr_rig.stat_ecs_blob_idxs[sbm_idx_glob],
			.mat_idx   = material_idx,
			.flags     = stat_flags
		};

		m_scene_queue.push(std::move(cmd));
	}
}


void SceneLayer::submit_skinned_geometry(uint32_t layer_idx)
{
	auto& rdr_rig = m_scene_rig.render_rig;
	
	m_registry.template scan<ecs::ModelComponent, ecs::AnimComponent>(
		[&rdr_rig, layer_idx, this](
			ecs::Entity                    entity,
			const ecs::ModelComponent&     model_comp,
			const ecs::AnimComponent&      anim_comp
		)
		{
			for (uint32_t sbm_idx_loc = 0; sbm_idx_loc < model_comp.sbm_count; ++sbm_idx_loc) {
				const uint32_t sbm_idx_glob = model_comp.sbm_first + sbm_idx_loc;
				const uint32_t material_idx = rdr_rig.skin_material_idxs[sbm_idx_glob];
				const auto& submesh         = rdr_rig.skin_submeshes[sbm_idx_glob];

				const uint64_t skin_sort_key =
					(static_cast<uint64_t>(material_idx      & 0xFFFFFFU) << 40) |
					(static_cast<uint64_t>(submesh.vtx_base  & 0xFFFFU)   << 24) |
					(static_cast<uint64_t>(submesh.idx_first & 0xFFFFFFU));

				m_anim_queue.push(rdr::AnimDrawCmd {
					.sort_key  = skin_sort_key,
					.layer_idx = layer_idx,
					.vtx_base  = submesh.vtx_base,
					.idx_first = submesh.idx_first,
					.idx_count = submesh.idx_count,
					.blob_idx  = rdr_rig.skin_ecs_blob_idxs[sbm_idx_glob],
					.mat_idx   = material_idx,
					.flags     = 0U
				});
			}
		}
	);
}


void SceneLayer::submit_debug_wires(uint32_t layer_idx)
{
	auto& rdr_rig         = m_scene_rig.render_rig;
	auto& cull_rig        = m_scene_rig.cull_rig;
	auto& spatial_rig     = m_scene_rig.spatial_rig;
	auto& cue_blob_mass   = m_staging_ctx.cue_blob_mass;
	auto& orl_blob_mass   = m_staging_ctx.orl_blob_mass;
	auto& cue_queue       = m_cue_queue;
	auto& overlay_queue   = m_overlay_queue;
	const auto& canonical = m_canonical;
	
	/* culling debug wires */

	if (m_show_cull) {

		/* occludee hull cues */

		for (uint32_t vis_idx = 0; vis_idx < rdr_rig.stat_sbms_visible.size(); ++vis_idx) {
			uint32_t sbm_idx_glob       = rdr_rig.stat_sbms_visible[vis_idx];
			const uint32_t occludee_idx = rdr_rig.stat_occludee_idxs[sbm_idx_glob];

			if (occludee_idx != 0xFFFFFFFFU) {
				const auto& hull_subwire = cull_rig.hull_subwires[occludee_idx];
				const mat4& mtx_M        = rdr_rig.stat_mtxs_M[sbm_idx_glob];

				uint32_t blob_idx = cue_blob_mass->push_raw({mtx_M});

				const uint32_t hull_cue_mask = 0U;
				const uint32_t palette_slice = 0U;
				const uint32_t tilemap_slice = 0U;

				const uint64_t hull_sort_key =
					(static_cast<uint64_t>(hull_cue_mask               & 0xFFFFU)   << 48) |
					(static_cast<uint64_t>(hull_subwire.vtx_base  & 0xFFFFU)   << 32) |
					(static_cast<uint64_t>(hull_subwire.idx_first & 0xFFFFFFU) << 8)  |
					(static_cast<uint64_t>(palette_slice          & 0x0FU)     << 4)  |
					(static_cast<uint64_t>(tilemap_slice          & 0x0FU));

				cue_queue.push(rdr::CueDrawCmd {
					.sort_key      = hull_sort_key,
					.layer_idx     = layer_idx,
					.vtx_base      = hull_subwire.vtx_base,
					.idx_first     = hull_subwire.idx_first,
					.idx_count     = hull_subwire.idx_count,
					.blob_idx      = blob_idx,
					.cue_mask      = hull_cue_mask,
					.tilemap_slice = tilemap_slice,
					.palette_slice = palette_slice
				});
			}
		}

		/* occluder twin overlays */

		m_registry.template scan<ecs::OccluderComponent, ecs::TransformComponent>(
			[&overlay_queue, &cull_rig, &orl_blob_mass, layer_idx](
				ecs::Entity                    entity,
				const ecs::OccluderComponent&  occluder,
				const ecs::TransformComponent& transform
			)
			{
				const mat4 mtx_M         = transform.mtx_W * occluder.mtx_L;
				const auto& twin_subwire = cull_rig.twin_subwires[occluder.twin_idx];

				vec4 wire_color   = vec4(1.0f, 0.0f, 0.5f, 0.1f);
				uint32_t blob_idx = orl_blob_mass->push_raw({mtx_M, wire_color});

				const uint32_t orl_flags = 1U;

				const uint64_t twin_sort_key =
					(static_cast<uint64_t>(orl_flags              & 0xFFFFU)   << 48) |
					(static_cast<uint64_t>(twin_subwire.vtx_base  & 0xFFFFU)   << 32) |
					(static_cast<uint64_t>(twin_subwire.idx_first & 0xFFFFFFU) << 8)  |
					(static_cast<uint64_t>(twin_subwire.idx_count & 0xFFU));

				overlay_queue.push(rdr::OverlayDrawCmd {
					.sort_key  = twin_sort_key,
					.layer_idx = layer_idx,
					.vtx_base  = twin_subwire.vtx_base,
					.idx_first = twin_subwire.idx_first,
					.idx_count = twin_subwire.idx_count,
					.blob_idx  = blob_idx,
					.flags     = orl_flags
				});
			}
		);
	}

	/* bvh debug wires */

	if (m_show_bvh) {
		auto box_geoslice =
			canonical.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::BoxWire)];

		const uint32_t vtx_base_glob  = canonical.vtx_base_wire;
		const uint32_t idx_first_glob = canonical.idx_first_wire + box_geoslice.idx_first;

		struct StackNode
		{
			uint32_t idx   {0U};
			uint32_t depth {0U};
		};

		auto push_bvh_cue_cmd = [
			&cue_blob_mass,
			&cue_queue,
			layer_idx,
			vtx_base_glob,
			idx_first_glob,
			idx_count = box_geoslice.idx_count
		](const vec3& min, const vec3& max, const mat4& mtx_parent, uint32_t tilemap_slice)
		{
			vec3 extent = max - min;
			vec3 center = min + extent * 0.5f;

			mat4 mtx_L = glm::translate(mat4(1.0f), center) * glm::scale(mat4(1.0f), extent);
			uint32_t blob_idx = cue_blob_mass->push_raw({mtx_parent * mtx_L});

			const uint32_t cue_mask      = 0U;
			const uint32_t palette_slice = 0U;

			const uint64_t sort_key =
				(static_cast<uint64_t>(cue_mask       & 0xFFFFU)   << 48) |
				(static_cast<uint64_t>(vtx_base_glob  & 0xFFFFU)   << 32) |
				(static_cast<uint64_t>(idx_first_glob & 0xFFFFFFU) << 8)  |
				(static_cast<uint64_t>(palette_slice  & 0x0FU)     << 4)  |
				(static_cast<uint64_t>(tilemap_slice  & 0x0FU));

			cue_queue.push(rdr::CueDrawCmd {
				.sort_key      = sort_key,
				.layer_idx     = layer_idx,
				.vtx_base      = vtx_base_glob,
				.idx_first     = idx_first_glob,
				.idx_count     = idx_count,
				.blob_idx      = blob_idx,
				.cue_mask      = cue_mask,
				.tilemap_slice = tilemap_slice,
				.palette_slice = palette_slice
			});
		};

		/* blas wire cues */

		for (uint32_t vis_idx = 0; vis_idx < rdr_rig.stat_sbms_visible.size(); ++vis_idx) {
			uint32_t sbm_idx_glob = rdr_rig.stat_sbms_visible[vis_idx];
			uint32_t blas_root    = spatial_rig.blas_sbm_roots[sbm_idx_glob];

			if (blas_root == 0xFFFFFFFFU) {
				continue;
			}

			StackNode blas_stack[cfg::bvh_stack_size];
			uint32_t stack_ptr = 0;
			blas_stack[stack_ptr++] = {blas_root, 0};

			const mat4 mtx_M = rdr_rig.stat_mtxs_M[sbm_idx_glob];

			while (stack_ptr > 0) {
				StackNode curr   = blas_stack[--stack_ptr];
				const auto& node = spatial_rig.blas_nodes[curr.idx];

				push_bvh_cue_cmd(node.min, node.max, mtx_M, 4U);

				if (curr.depth < cfg::max_depth_blas) {
					vec3 scale = (node.max - node.min) / 255.0f;

					for (int child_idx = 0; child_idx < 8; ++child_idx) {
						if (node.x_min[child_idx] == 255U)
							continue;

						vec3 child_min =
							node.min + vec3(
								node.x_min[child_idx],
								node.y_min[child_idx],
								node.z_min[child_idx])
							* scale;

						vec3 child_max =
							node.min + vec3(
								node.x_max[child_idx],
								node.y_max[child_idx],
								node.z_max[child_idx])
							* scale;

						push_bvh_cue_cmd(child_min, child_max, mtx_M, 4U);

						if (node.tris_cnts[child_idx] == 0) {
							blas_stack[stack_ptr++] = {node.base_idxs[child_idx], curr.depth + 1};
						}
					}
				}
			}
		}

		/* tlas wire cues */

		if (!spatial_rig.tlas_nodes.empty()) {
			StackNode tlas_stack[cfg::bvh_stack_size];
			uint32_t stack_ptr = 0;
			tlas_stack[stack_ptr++] = {spatial_rig.tlas_root_idx, 0};

			const mat4 identity = mat4(1.0f);

			while (stack_ptr > 0) {
				StackNode curr   = tlas_stack[--stack_ptr];
				const auto& node = spatial_rig.tlas_nodes[curr.idx];

				for (int child_idx = 0; child_idx < 8; ++child_idx) {

					if (node.x_min[child_idx] == std::numeric_limits<float>::max()) {
						continue;
					}

					vec3 child_min = vec3(
						node.x_min[child_idx],
						node.y_min[child_idx],
						node.z_min[child_idx]
					);

					vec3 child_max = vec3(
						node.x_max[child_idx],
						node.y_max[child_idx],
						node.z_max[child_idx]
					);

					push_bvh_cue_cmd(child_min, child_max, identity, 3U);

					if (node.blas_count[child_idx] == 0 && curr.depth < cfg::max_depth_tlas) {
						tlas_stack[stack_ptr++] = {node.blas_first[child_idx], curr.depth + 1};
					}
				}
			}
		}

		/* raycast blas wire overlays */

		if (m_selection.submesh != std::numeric_limits<uint32_t>::max()) {
			auto box_geoslice =
				canonical.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::BoxWire)];

			const uint32_t vtx_base_glob  = canonical.vtx_base_wire;
			const uint32_t idx_first_glob = canonical.idx_first_wire + box_geoslice.idx_first;
			const uint32_t idx_count      = box_geoslice.idx_count;

			auto push_bvh_overlay_cmd = [
				&orl_blob_mass,
				&overlay_queue,
				layer_idx,
				vtx_base_glob,
				idx_first_glob,
				idx_count
			](const vec3& min, const vec3& max, const mat4& mtx_parent, const vec4& color)
			{
				vec3 extent = max - min;
				vec3 center = min + extent * 0.5f;

				mat4 mtx_L = glm::translate(mat4(1.0f), center) * glm::scale(mat4(1.0f), extent);
				uint32_t blob_idx = orl_blob_mass->push_raw({mtx_parent * mtx_L, color});

				const uint32_t orl_flags = 1U;

				const uint64_t sort_key =
					(static_cast<uint64_t>(orl_flags      & 0xFFFFU)   << 48) |
					(static_cast<uint64_t>(vtx_base_glob  & 0xFFFFU)   << 32) |
					(static_cast<uint64_t>(idx_first_glob & 0xFFFFFFU) << 8)  |
					(static_cast<uint64_t>(idx_count      & 0xFFU));

				overlay_queue.push(rdr::OverlayDrawCmd {
					.sort_key  = sort_key,
					.layer_idx = layer_idx,
					.vtx_base  = vtx_base_glob,
					.idx_first = idx_first_glob,
					.idx_count = idx_count,
					.blob_idx  = blob_idx,
					.flags     = orl_flags
				});
			};

			const mat4 mtx_M = rdr_rig.stat_mtxs_M[m_selection.submesh];

			const vec4 tested_color = vec4(0.333f, 0.0f, 1.0f, 0.8f);
			for (uint32_t i = 0; i < m_pick_ctx.tested_node_cnt; ++i) {
				const uint32_t node_idx = m_pick_ctx.tested_node_idxs[i];
				
				bool is_path_node = false;
				for (uint32_t j = 0; j < m_pick_ctx.path_node_cnt; ++j) {
					if (m_pick_ctx.path_node_idxs[j] == node_idx) {
						is_path_node = true;
						break;
					}
				}

				if (is_path_node) {
					continue;
				}

				const auto& blas_node = spatial_rig.blas_nodes[node_idx];
				push_bvh_overlay_cmd(blas_node.min, blas_node.max, mtx_M, tested_color);
			}

			const vec4 path_color = vec4(1.0f, 0.0f, 0.251f, 0.8f);
			for (uint32_t i = 0; i < m_pick_ctx.path_node_cnt; ++i) {
				const uint32_t node_idx  = m_pick_ctx.path_node_idxs[i];
				const auto&    blas_node = spatial_rig.blas_nodes[node_idx];

				push_bvh_overlay_cmd(blas_node.min, blas_node.max, mtx_M, path_color);
			}
		}
	}
}


void SceneLayer::cull(void* job_input_ptr)
{
	auto* slice          = static_cast<CullJobSlice*>(job_input_ptr);
	const auto* cull_rig = slice->cull_rig;

	/* spatial output closure */

	auto push_visible_submesh = [slice](uint32_t sbm_idx_push)
	{
		uint32_t idx = slice->async_result->count++;
		slice->async_result->sbms_visible[idx] = sbm_idx_push;
	};

	/* occlusion test closure */

	auto test_occlusion = [&push_visible_submesh, slice, cull_rig](uint32_t sbm_idx_cull)
	{
		const uint32_t occludee_idx = slice->occludee_idxs[sbm_idx_cull];
		const auto& hull_slice      = cull_rig->hull_geoslices[occludee_idx];

		if (!hull_slice.is_valid()) {
			push_visible_submesh(sbm_idx_cull);
			return;
		}

		const mat4& mtx_M   = slice->mtxs_M[sbm_idx_cull];
		const mat4  mtx_MVP = slice->mtx_VP * mtx_M;

		float x_min_ndc  = std::numeric_limits<float>::max();
		float y_min_ndc  = std::numeric_limits<float>::max();
		float x_max_ndc  = std::numeric_limits<float>::lowest();
		float y_max_ndc  = std::numeric_limits<float>::lowest();
		float z_nearest  = 1.0f;
		float z_farthest = std::numeric_limits<float>::lowest();

		bool crosses_near_plane = false;

		const uint32_t vtx_end = hull_slice.vtx_base + hull_slice.vtx_count;

		for (uint32_t vtx = hull_slice.vtx_base; vtx < vtx_end; ++vtx) {

			vec4 pos_clip = mtx_MVP * vec4(cull_rig->hull_positions[vtx], 1.0f);

			if (pos_clip.w < 0.1f) {
				crosses_near_plane = true;
				break;
			}

			const float w_inv = 1.0f / pos_clip.w;
			float z_norm      = (pos_clip.z * w_inv) * 0.5f + 0.5f;
			z_norm            = std::clamp(z_norm, 0.0f, 1.0f);

			z_nearest  = std::min(z_nearest,  z_norm);
			z_farthest = std::max(z_farthest, z_norm);

			x_min_ndc  = std::min(x_min_ndc, pos_clip.x * w_inv);
			y_min_ndc  = std::min(y_min_ndc, pos_clip.y * w_inv);
			x_max_ndc  = std::max(x_max_ndc, pos_clip.x * w_inv);
			y_max_ndc  = std::max(y_max_ndc, pos_clip.y * w_inv);
		}

		if (crosses_near_plane) {
			push_visible_submesh(sbm_idx_cull);
			return;
		}

		++slice->async_result->occlusion_tested;

		const float w_px = (x_max_ndc - x_min_ndc) * 0.5f * static_cast<float>(HiZBuffer::width);
		const float h_px = (y_max_ndc - y_min_ndc) * 0.5f * static_cast<float>(HiZBuffer::height);

		int mip = std::clamp(static_cast<int>(std::log2(std::max({w_px, h_px, 1.0f}))), 0, 2);

		const int w_mip = static_cast<int>(HiZBuffer::width)  >> mip;
		const int h_mip = static_cast<int>(HiZBuffer::height) >> mip;

		int32_t x_min_full_px =
			static_cast<int32_t>(std::floor((x_min_ndc * 0.5f + 0.5f) *
			static_cast<float>(HiZBuffer::width)));

		int32_t x_max_full_px =
			static_cast<int32_t>(std::ceil((x_max_ndc  * 0.5f + 0.5f) *
			static_cast<float>(HiZBuffer::width)));

		int32_t y_min_full_px =
			static_cast<int32_t>(std::floor((y_min_ndc * 0.5f + 0.5f) *
			static_cast<float>(HiZBuffer::height)));

		int32_t y_max_full_px =
			static_cast<int32_t>(std::ceil((y_max_ndc  * 0.5f + 0.5f) *
			static_cast<float>(HiZBuffer::height)));

		int x_min_mip_px = std::clamp(x_min_full_px >> mip, 0, w_mip - 1);
		int x_max_mip_px = std::clamp(x_max_full_px >> mip, 0, w_mip - 1);
		int y_min_mip_px = std::clamp(y_min_full_px >> mip, 0, h_mip - 1);
		int y_max_mip_px = std::clamp(y_max_full_px >> mip, 0, h_mip - 1);

		float max_depth = std::numeric_limits<float>::lowest();

		for (int32_t y = y_min_mip_px; y <= y_max_mip_px; ++y) {
			int32_t row_offset = y * w_mip;
			for (int32_t x = x_min_mip_px; x <= x_max_mip_px; ++x) {
				max_depth = std::max(
					max_depth,
					slice->hiz_buffer->mips[mip][static_cast<size_t>(row_offset + x)]
				);
			}
		}

		if (z_nearest > max_depth) {
			++slice->async_result->occlusion_culled;
		}
		else {
			push_visible_submesh(sbm_idx_cull);
		}
	};

	/* frustum data setup */

	__m256 plane_nrm_x[math::frustum_plane_cnt];
	__m256 plane_nrm_y[math::frustum_plane_cnt];
	__m256 plane_nrm_z[math::frustum_plane_cnt];
	__m256 plane_offset[math::frustum_plane_cnt];
	__m256 plane_nrm_abs_x[math::frustum_plane_cnt];
	__m256 plane_nrm_abs_y[math::frustum_plane_cnt];
	__m256 plane_nrm_abs_z[math::frustum_plane_cnt];

	for (uint32_t plane_idx = 0; plane_idx < math::frustum_plane_cnt; ++plane_idx) {
		const auto& plane = slice->frustum_planes[plane_idx];
		plane_nrm_x[plane_idx]     = _mm256_set1_ps(plane.nrm.x);
		plane_nrm_y[plane_idx]     = _mm256_set1_ps(plane.nrm.y);
		plane_nrm_z[plane_idx]     = _mm256_set1_ps(plane.nrm.z);
		plane_offset[plane_idx]    = _mm256_set1_ps(plane.offset);
		plane_nrm_abs_x[plane_idx] = _mm256_set1_ps(plane.nrm_abs.x);
		plane_nrm_abs_y[plane_idx] = _mm256_set1_ps(plane.nrm_abs.y);
		plane_nrm_abs_z[plane_idx] = _mm256_set1_ps(plane.nrm_abs.z);
	}

	const __m256 half_scalar = _mm256_set1_ps(0.5f);

	/* frustum culling: main loop */

	uint32_t sbm_idx = slice->begin;

	for (; sbm_idx + 7 < slice->end; sbm_idx += 8) {

		__m256 min_x = _mm256_loadu_ps(&slice->aabb_min_x[sbm_idx]);
		__m256 min_y = _mm256_loadu_ps(&slice->aabb_min_y[sbm_idx]);
		__m256 min_z = _mm256_loadu_ps(&slice->aabb_min_z[sbm_idx]);

		__m256 max_x = _mm256_loadu_ps(&slice->aabb_max_x[sbm_idx]);
		__m256 max_y = _mm256_loadu_ps(&slice->aabb_max_y[sbm_idx]);
		__m256 max_z = _mm256_loadu_ps(&slice->aabb_max_z[sbm_idx]);

		__m256 center_x = _mm256_mul_ps(_mm256_add_ps(min_x, max_x), half_scalar);
		__m256 center_y = _mm256_mul_ps(_mm256_add_ps(min_y, max_y), half_scalar);
		__m256 center_z = _mm256_mul_ps(_mm256_add_ps(min_z, max_z), half_scalar);

		__m256 half_ext_x = _mm256_mul_ps(_mm256_sub_ps(max_x, min_x), half_scalar);
		__m256 half_ext_y = _mm256_mul_ps(_mm256_sub_ps(max_y, min_y), half_scalar);
		__m256 half_ext_z = _mm256_mul_ps(_mm256_sub_ps(max_z, min_z), half_scalar);

		__m256 inside_mask = _mm256_castsi256_ps(_mm256_set1_epi32(-1));

		for (uint32_t plane_idx = 0; plane_idx < math::frustum_plane_cnt; ++plane_idx) {

			__m256 center_dist = _mm256_fmadd_ps(
				center_x,
				plane_nrm_x[plane_idx],
				_mm256_fmadd_ps(
					center_y,
					plane_nrm_y[plane_idx],
					_mm256_fmadd_ps(
						center_z,
						plane_nrm_z[plane_idx],
						plane_offset[plane_idx]
					)
				)
			);

			__m256 rad_projection = _mm256_fmadd_ps(
				half_ext_x,
				plane_nrm_abs_x[plane_idx],
				_mm256_fmadd_ps(
					half_ext_y,
					plane_nrm_abs_y[plane_idx],
					_mm256_mul_ps(
						half_ext_z,
						plane_nrm_abs_z[plane_idx]
					)
				)
			);

			__m256 cull_pass_mask = _mm256_cmp_ps(
				_mm256_add_ps(center_dist, rad_projection),
				_mm256_setzero_ps(),
				_CMP_GE_OQ
			);

			inside_mask = _mm256_and_ps(inside_mask, cull_pass_mask);
		}

		uint32_t active_lane_mask =
			static_cast<uint32_t>(_mm256_movemask_ps(inside_mask));

		slice->async_result->frustum_tested += 8;
		slice->async_result->frustum_culled += (8 - std::popcount(active_lane_mask));

		while (active_lane_mask) {
			uint32_t active_lane_idx =
				static_cast<uint32_t>(std::countr_zero(active_lane_mask));

			active_lane_mask &= (active_lane_mask - 1);

			uint32_t curr_sbm = sbm_idx + active_lane_idx;
			test_occlusion(curr_sbm);
		}
	}

	/* frustum culling: tail loop */

	for (; sbm_idx < slice->end; ++sbm_idx) {

		const float center_x   = (slice->aabb_min_x[sbm_idx] + slice->aabb_max_x[sbm_idx]) * 0.5f;
		const float center_y   = (slice->aabb_min_y[sbm_idx] + slice->aabb_max_y[sbm_idx]) * 0.5f;
		const float center_z   = (slice->aabb_min_z[sbm_idx] + slice->aabb_max_z[sbm_idx]) * 0.5f;
		const float half_ext_x = (slice->aabb_max_x[sbm_idx] - slice->aabb_min_x[sbm_idx]) * 0.5f;
		const float half_ext_y = (slice->aabb_max_y[sbm_idx] - slice->aabb_min_y[sbm_idx]) * 0.5f;
		const float half_ext_z = (slice->aabb_max_z[sbm_idx] - slice->aabb_min_z[sbm_idx]) * 0.5f;

		bool is_culled = false;
		for (uint32_t plane_idx = 0; plane_idx < math::frustum_plane_cnt; ++plane_idx) {

			const auto& plane = slice->frustum_planes[plane_idx];
			
			const float center_dist =
				center_x * plane.nrm.x + center_y * plane.nrm.y + center_z * plane.nrm.z + plane.offset;

			const float rad_projection =
				half_ext_x * plane.nrm_abs.x + half_ext_y * plane.nrm_abs.y + half_ext_z * plane.nrm_abs.z;

			slice->async_result->frustum_tested++;

			if (center_dist < -rad_projection) {
				is_culled = true;
				break;
			}
		}

		if (is_culled) {
			slice->async_result->frustum_culled++;
			continue;
		}

		test_occlusion(sbm_idx);
	}
}


void SceneLayer::process_commands(CmdStream::Reader reader)
{
	CmdType     type;
	const void* payload;
	uint32_t    payload_size;

	while (reader.next(type, payload, payload_size))
	{
		switch (type)
		{
			case CmdType::SetTransform:
			{
				HPR_ASSERT(payload_size >= sizeof(SetTransform));

				const SetTransform* cmd = static_cast<const SetTransform*>(payload);

				if (auto* transform_comp = m_registry.get<ecs::TransformComponent>(cmd->entity)) {
					transform_comp->position = cmd->transform.position;
					transform_comp->rotation = cmd->transform.rotation;
					transform_comp->scale    = cmd->transform.scale;
				}
				break;
			}
			case CmdType::SetLight:
			{
				HPR_ASSERT(payload_size >= sizeof(SetLight));

				const SetLight* cmd = static_cast<const SetLight*>(payload);

				if (auto* light_comp = m_registry.get<ecs::LightComponent>(cmd->entity)) {
					light_comp->enabled   = cmd->light.enabled;
					light_comp->type      = static_cast<scn::LightType>(cmd->light.type);
					light_comp->color_rgb = cmd->light.color_rgb;
					light_comp->intensity = cmd->light.intensity;
					light_comp->range     = cmd->light.range;
					light_comp->inner_deg = cmd->light.inner_deg;
					light_comp->outer_deg = cmd->light.outer_deg;
				}
				break;
			}
			case CmdType::SetMaterial:
			{
/*
				HPR_ASSERT(payload_size >= sizeof(SetMaterial));

				const SetMaterial* cmd = static_cast<const SetMaterial*>(payload);
				const auto* model_comp = m_registry.get<ecs::ModelComponent>(cmd->entity);

				if (!model_comp)
					break;

				HPR_ASSERT(cmd->submesh < model_comp->prim_count);

				const uint32_t scene_prim_index = model_comp->prim_first + cmd->submesh;
				const auto& scene_prims = m_scene.render_rig.primitives;

				HPR_ASSERT(scene_prim_index < scene_prims.size());

				const auto& primitive = scene_prims[scene_prim_index];

				if (auto* material_inst = m_resolver.resolve<rdr::MaterialInstance>(primitive.material)) {
					material_inst->albedo_tint      = cmd->albedo_tint;
					material_inst->metallic_factor  = cmd->metallic_factor;
					material_inst->roughness_factor = cmd->roughness_factor;
					material_inst->normal_scale     = cmd->normal_scale;
					material_inst->ao_strength      = cmd->ao_strength;
					material_inst->emissive_factor  = cmd->emissive_factor;
					material_inst->uv_scale         = cmd->uv_scale;
					material_inst->uv_offset        = cmd->uv_offset;
				}
*/
				break;
			}
			default:
			{
				break;
			}
		}
	}
}


edt::InspectorSnapshot SceneLayer::selection_properties() const
{
	edt::InspectorSnapshot inspector_snapshot;
/*
	const ecs::Entity entity = m_selection.entity;
	if (entity == ecs::ctx::invalid_entity) {
		return inspector_snapshot;
	}

	if (const auto* light = m_registry.get<ecs::LightComponent>(entity)) {
		inspector_snapshot.has_light = true;
		inspector_snapshot.light.enabled   = light->enabled ? 1 : 0;
		inspector_snapshot.light.color_rgb = light->color_rgb;
		inspector_snapshot.light.intensity = light->intensity;
		inspector_snapshot.light.range     = light->range;

		inspector_snapshot.light.inner_deg = std::cos(glm::radians(light->inner_deg));
		inspector_snapshot.light.outer_deg = std::cos(glm::radians(light->outer_deg));
	}

	if (const auto* material = m_registry.get<ecs::ModelComponent>(entity)) {
		inspector_snapshot.submesh_count = material->prim_count;
		if (m_selection.submesh < material->prim_count) {
			const auto& prim = m_scene.render_rig.primitives[material->prim_first + m_selection.submesh];
			inspector_snapshot.has_material = true;
			inspector_snapshot.material = prim.material;
		}
	}
*/
	return inspector_snapshot;
}


void SceneLayer::on_result(Event& event)
{
	(void) event;
}

} // hpr::lyr

