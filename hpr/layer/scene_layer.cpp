#include "scene_layer.hpp"

#include "draw_queue_data.hpp"
#include "math.hpp"
#include "panic.hpp"
#include "event.hpp"

#include "scene_query.hpp"
#include "scene_context.hpp"
#include "stats.hpp"
#include "systems_scene.hpp"
#include "draw_view_data.hpp"

#include <bit>
#include <limits>
#include <chrono>
#include <immintrin.h>


namespace hpr {


SceneLayer::SceneLayer(
	scn::Scene                             scene,
	MainRegistry&                          ecs_registry,
	mtp::shared<mtp_scn_set>&              metapool,
	rdr::SurfaceInfo                       surface_info,
	rdr::StagingContext                    staging_ctx,
	rdr::RenderQueue<rdr::SceneDrawCmd>&   scene_queue,
	rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
	rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue,
	log::StatsHarvester&                   harvester
)
	: m_scene         {std::move(scene)}
	, m_registry      {ecs_registry}
	, m_metapool      {metapool}
	, m_surface_info  {surface_info}
	, m_staging_ctx   {staging_ctx}
	, m_scene_queue   {scene_queue}
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
	m_scene.clear_volatile();

	ecs::TransformSystem::update(m_registry);
}


void SceneLayer::on_detach()
{}


bool SceneLayer::on_event(Event& event)
{
	return false;
}


bool SceneLayer::on_actions(
	const scn::SceneContext& scene_ctx,
	std::span<const Action>  actions
)
{
	bool action_consumed = false;

	for (const Action& action : actions) {
		switch (action.kind) {

		case ActionKind::SelectClick:
		{
			const auto& payload = std::get<SelectClickAction>(action.payload);

			scn::Ray ray = scn::make_pick_ray(
				payload.x,
				payload.y,
				m_surface_info.width,
				m_surface_info.height,
				scene_ctx.draw_view
			);

			const scn::RayHit ray_hit = raycast_scene(
				ray,
				m_scene,
				m_staging_ctx
			);

			if (ray_hit.is_hit) {
				m_selection.entity = ray_hit.entity;

				const auto* trs_comp =
					m_registry.get<ecs::TransformComponent>(m_selection.entity);
				HPR_ASSERT(trs_comp);

				m_selection.transform.position = trs_comp->position;
				m_selection.transform.rotation = trs_comp->rotation;
				m_selection.transform.scale    = trs_comp->scale;

				m_selection.submesh = ray_hit.submesh;
			}
			else {
				m_selection.transform = {};
				m_selection.entity    = ecs::ctx::invalid_entity;
				m_selection.submesh   = std::numeric_limits<uint32_t>::max();
			}

			auto* event = m_event_queue->push<SelectionChangedEvent>();

			event->selection.transform = m_selection.transform;
			event->selection.entity    = m_selection.entity;
			event->selection.submesh   = m_selection.submesh;
			event->emitter = this;

			action_consumed = true;

			break;
		}

		case ActionKind::DebugToggleCulling:
		{
			m_show_cull_wireframes = !m_show_cull_wireframes;

			break;
		}

		default: break;
		}
	}
	return action_consumed;
}


void SceneLayer::on_update(scn::SceneContext& scene_ctx, float delta_time)
{
	m_scene.clear_volatile();

	const size_t total_prims = m_scene.render_rig.primitives.size();
	m_scene.render_rig.resize_aabb_world(total_prims);
	m_scene.render_rig.ecs_trs_idxs.resize(total_prims);

	const size_t total_occluders = m_registry.template size<ecs::OccluderComponent>();
	m_scene.cull_rig.occluder_matrices_MVP.reserve(total_occluders);
	m_scene.cull_rig.occluder_idxs.reserve(total_occluders);

	m_staging_ctx.scn_trs_mass->clear();

	scene_ctx.light_set.ambient_rgb = m_scene.render_rig.ambient_rgb;

	scene_ctx.light_set = ecs::LightSystem::build_light(
		m_registry,
		scene_ctx.draw_view
	);

	for (auto& mip : m_hiz_buffer.mips) {
		std::fill(mip.begin(), mip.end(), 1.0f);
	}
}


void SceneLayer::on_submit(const scn::SceneContext& scene_ctx, uint32_t layer_idx)
{
	auto& rdr_rig              = m_scene.render_rig;
	auto& cull_rig             = m_scene.cull_rig;
	auto& model_trs_mass       = m_staging_ctx.scn_trs_mass;
	auto& cue_trs_mass         = m_staging_ctx.cue_trs_mass;
	auto& orl_trs_mass         = m_staging_ctx.orl_trs_mass;
	auto& cue_queue            = m_cue_queue;
	auto& overlay_queue        = m_overlay_queue;
	const auto& selection      = m_selection.entity;
	const auto& scene_prims    = rdr_rig.primitives;
	const uint32_t total_prims = static_cast<uint32_t>(scene_prims.size());
	bool show_cull             = m_show_cull_wireframes;

	HPR_ASSERT(total_prims < cfg::max_scene_prims);
	if (total_prims == 0) {
		return;
	}

	/* ecs sync: models */

	m_registry.template scan<ecs::ModelComponent, ecs::TransformComponent>(
		[&rdr_rig, &cull_rig, &model_trs_mass, &cue_trs_mass, &cue_queue, layer_idx, show_cull](
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

			for (uint32_t prim_idx_loc = 0; prim_idx_loc < model.prim_count; ++prim_idx_loc) {

				const uint32_t prim_idx_glob = model.prim_first + prim_idx_loc;
				const auto& prim = rdr_rig.primitives[prim_idx_glob];

				const mat4 mtx_M =      mtx_W  * prim.mtx_L;
				const mat4 mtx_N = mat4(mtx_WN * prim.mtx_LN);

				rdr_rig.matrices_M[prim_idx_glob] = mtx_M;

				rdr_rig.ecs_trs_idxs[prim_idx_glob] =
					model_trs_mass->push_raw({mtx_M, mtx_N, prim.material_idx});

				const scn::AABB& aabb_local = rdr_rig.aabb_local[prim_idx_glob];

				const vec3 center_local = (aabb_local.max + aabb_local.min) * 0.5f;
				const vec3 half_local   = (aabb_local.max - aabb_local.min) * 0.5f;

				const vec3 center_world = vec3(mtx_W * vec4(center_local, 1.0f));
				const vec3 half_world   = mtx_W_abs * half_local;

				const vec3 pos_min = center_world - half_world;
				const vec3 pos_max = center_world + half_world;

				rdr_rig.aabb_world_min_x[prim_idx_glob] = pos_min.x;
				rdr_rig.aabb_world_min_y[prim_idx_glob] = pos_min.y;
				rdr_rig.aabb_world_min_z[prim_idx_glob] = pos_min.z;
				rdr_rig.aabb_world_max_x[prim_idx_glob] = pos_max.x;
				rdr_rig.aabb_world_max_y[prim_idx_glob] = pos_max.y;
				rdr_rig.aabb_world_max_z[prim_idx_glob] = pos_max.z;

				if (show_cull) {

					const uint32_t occludee_idx = rdr_rig.occludee_idxs[prim_idx_glob];

					if (occludee_idx != 0xFFFFFFFF) {

						const auto& hull_subwire = cull_rig.hull_subwires[occludee_idx];
						
						uint32_t trs_idx = cue_trs_mass->push_raw({mtx_M});

						const uint32_t cue_mask      = 0U;
						const uint32_t palette_slice = 0U;
						const uint32_t tilemap_slice = 3;

						const uint64_t twin_cue_sort_key =
							(static_cast<uint64_t>(cue_mask               & 0xFFFFU)   << 48) |
							(static_cast<uint64_t>(hull_subwire.vtx_base  & 0xFFFFU)   << 32) |
							(static_cast<uint64_t>(hull_subwire.idx_first & 0xFFFFFFU) << 8)  |
							(static_cast<uint64_t>(palette_slice          & 0x0FU)     << 4)  |
							(static_cast<uint64_t>(tilemap_slice          & 0x0FU));

						cue_queue.push(rdr::CueDrawCmd {
							.sort_key      = twin_cue_sort_key,
							.layer_idx     = layer_idx,
							.vtx_base      = hull_subwire.vtx_base,
							.idx_first     = hull_subwire.idx_first,
							.idx_count     = hull_subwire.idx_count,
							.trs_idx       = trs_idx,
							.cue_mask      = 0U,
							.tilemap_slice = 0U,
							.palette_slice = 0U
						});
					}
				}
			}
		}
	);

	/* ecs sync: occluders */

	m_registry.template scan<ecs::OccluderComponent, ecs::TransformComponent>(
		[&overlay_queue, &cull_rig, &scene_ctx, &orl_trs_mass, layer_idx, show_cull](
			ecs::Entity                    entity,
			const ecs::OccluderComponent&  occluder,
			const ecs::TransformComponent& transform
		)
		{
			const mat4 mtx_M   = transform.mtx_W * occluder.mtx_L;
			const mat4 mtx_MVP = scene_ctx.draw_view.mtx_VP * mtx_M;

			cull_rig.occluder_matrices_MVP.push_back(mtx_MVP);
			cull_rig.occluder_idxs.push_back(occluder.twin_idx);

			if (show_cull) {

				const auto& twin_subwire = cull_rig.twin_subwires[occluder.twin_idx];

				vec4 wire_color  = vec4(1.0f, 0.0f, 0.5f, 0.1f);
				uint32_t trs_idx = orl_trs_mass->push_raw({mtx_M, wire_color});

				const uint32_t cmd_flags = 1U;

				const uint64_t twin_orl_sort_key =
					(static_cast<uint64_t>(cmd_flags              & 0xFFFFU)   << 48) |
					(static_cast<uint64_t>(twin_subwire.vtx_base  & 0xFFFFU)   << 32) |
					(static_cast<uint64_t>(twin_subwire.idx_first & 0xFFFFFFU) << 8)  |
					(static_cast<uint64_t>(twin_subwire.idx_count & 0xFFU));

				overlay_queue.push(rdr::OverlayDrawCmd {
					.sort_key  = twin_orl_sort_key,
					.layer_idx = layer_idx,
					.vtx_base  = twin_subwire.vtx_base,
					.idx_first = twin_subwire.idx_first,
					.idx_count = twin_subwire.idx_count,
					.trs_idx   = trs_idx,
					.flags     = cmd_flags
				});
			}
		}
	);

	/* hiz depth prepass: rasterize low-poly twins */

	auto raster_start = std::chrono::high_resolution_clock::now();

	const float hiz_width  = static_cast<float>(HiZBuffer::width);
	const float hiz_height = static_cast<float>(HiZBuffer::height);

	const uint32_t total_occluders = static_cast<uint32_t>(cull_rig.occluder_idxs.size());

	for (uint32_t occr_idx = 0; occr_idx < total_occluders; ++occr_idx) {

		const uint32_t twin_idx = cull_rig.occluder_idxs[occr_idx];
		const auto& twin_slice  = cull_rig.twin_geoslices[twin_idx];

		HPR_ASSERT_MSG(twin_slice.is_valid(),
			"occluder twin geometry pipeline fail");

		const mat4 mtx_MVP = cull_rig.occluder_matrices_MVP[occr_idx];

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

			int32_t min_x = std::clamp(static_cast<int32_t>(std::min({vtx_0_px.x, vtx_1_px.x, vtx_2_px.x})), 0, static_cast<int32_t>(HiZBuffer::width  - 1));
			int32_t min_y = std::clamp(static_cast<int32_t>(std::min({vtx_0_px.y, vtx_1_px.y, vtx_2_px.y})), 0, static_cast<int32_t>(HiZBuffer::height - 1));
			int32_t max_x = std::clamp(static_cast<int32_t>(std::max({vtx_0_px.x, vtx_1_px.x, vtx_2_px.x})), 0, static_cast<int32_t>(HiZBuffer::width  - 1));
			int32_t max_y = std::clamp(static_cast<int32_t>(std::max({vtx_0_px.y, vtx_1_px.y, vtx_2_px.y})), 0, static_cast<int32_t>(HiZBuffer::height - 1));

			auto edge_func = [](const vec2& a, const vec2& b, const vec2& c) {
				return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
			};

			float area = edge_func(vtx_0_px, vtx_1_px, vtx_2_px);
			if (area <= 0.0f) {
				continue;
			}

			for (int32_t y = min_y; y <= max_y; ++y) {

				int32_t row_offset = y * static_cast<int32_t>(HiZBuffer::width);

				for (int32_t x = min_x; x <= max_x; ++x) {

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

	constexpr int32_t num_mips = 3; 

	for (int32_t mip_idx = 1; mip_idx < num_mips; ++mip_idx) {

		const int32_t w_mip_src = static_cast<int32_t>(HiZBuffer::width)  >> (mip_idx - 1);
		const int32_t h_mip_src = static_cast<int32_t>(HiZBuffer::height) >> (mip_idx - 1);

		const int32_t w_mip_dst = w_mip_src >> 1;
		const int32_t h_mip_dst = h_mip_src >> 1;

		const float* src_mip = m_hiz_buffer.mips[mip_idx - 1].data();
		float* dst_mip       = m_hiz_buffer.mips[mip_idx].data();

		for (int32_t y = 0; y < h_mip_dst; ++y) {
			for (int32_t x = 0; x < w_mip_dst; ++x) {

				const int32_t src_idx = (y * 2 * w_mip_src) + (x * 2);

				const float z_00 = src_mip[src_idx];
				const float z_01 = src_mip[src_idx + 1];
				const float z_10 = src_mip[src_idx + w_mip_src];
				const float z_11 = src_mip[src_idx + w_mip_src + 1];

				dst_mip[y * w_mip_dst + x] =
					std::max(std::max(z_00, z_01), std::max(z_10, z_11));
			}
		}
	}

	/* frustum extract */

	std::array<FrustumPlane, math::frustum_plane_cnt> frustum_planes;

	for (size_t i = 0; i < math::frustum_plane_cnt; ++i) {

		const vec4 plane_raw = scene_ctx.draw_view.frustum[i];

		const vec3 nrm_raw {
			plane_raw.x,
			plane_raw.y,
			plane_raw.z
		};

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

	const uint32_t slice_count =
		(total_prims + cfg::job_grain - 1) / cfg::job_grain;

	mtp::slag<CullJobSlice, mtp::default_set> cull_slices(slice_count, CullJobSlice {});

	mtp::slag<CmdAsyncResult, mtp_scn_set> cmd_async_results {m_metapool};
	cmd_async_results.resize(slice_count);

	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {

		auto& cull_slice = cull_slices[slice_idx];

		cull_slice.begin = slice_idx * cfg::job_grain;
		cull_slice.end   = std::min(cull_slice.begin + cfg::job_grain, total_prims);

		cull_slice.scene_primitives = rdr_rig.primitives.data();
		cull_slice.ecs_trs_idxs     = rdr_rig.ecs_trs_idxs.data();
		cull_slice.matrices_M       = rdr_rig.matrices_M.data();

		cull_slice.aabb_min_x = rdr_rig.aabb_world_min_x.data();
		cull_slice.aabb_min_y = rdr_rig.aabb_world_min_y.data();
		cull_slice.aabb_min_z = rdr_rig.aabb_world_min_z.data();
		cull_slice.aabb_max_x = rdr_rig.aabb_world_max_x.data();
		cull_slice.aabb_max_y = rdr_rig.aabb_world_max_y.data();
		cull_slice.aabb_max_z = rdr_rig.aabb_world_max_z.data();

		cull_slice.frustum_planes = frustum_planes.data();

		cull_slice.cull_rig      = &cull_rig;
		cull_slice.occludee_idxs = rdr_rig.occludee_idxs.data();

		cull_slice.mtx_VP     = scene_ctx.draw_view.mtx_VP;
		cull_slice.hiz_buffer = &m_hiz_buffer;

		cull_slice.layer_idx       = layer_idx;
		cull_slice.selected_entity = selection;

		cull_slice.async_result = &cmd_async_results[slice_idx];
		cull_slice.async_result->clear();
	}

	/* culling dispatch & result */

	auto cull_start_time = std::chrono::steady_clock::now();

	job::JobLatch job_latch;

	m_job_scheduler.dispatch_range(
		job_latch,
		&SceneLayer::cull_generate_cmds,
		slice_count,
		1,
		cull_slices.data()
	);

	job_latch.wait();

	auto cull_end_time = std::chrono::steady_clock::now();

	auto& scn_stats = m_harvester.scene_layer_curr();
	
	scn_stats.cull_job_ms =
		std::chrono::duration<double, std::milli>(cull_end_time - cull_start_time).count();

	size_t prims_vis_total = 0;
	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		prims_vis_total += cmd_async_results[slice_idx].count;
	}

	rdr_rig.prims_visible.reserve(prims_vis_total);

	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		auto& async_result = cmd_async_results[slice_idx];

		scn_stats.frust_tested += async_result.frustum_tested;
		scn_stats.frust_culled += async_result.frustum_culled;

		scn_stats.occl_tested += async_result.occlusion_tested;
		scn_stats.occl_culled += async_result.occlusion_culled;

		for (uint32_t i = 0; i < async_result.count; ++i) {
			m_scene_queue.push(std::move(async_result.cmds[i]));
			rdr_rig.prims_visible.push_back(async_result.prims_visible[i]);
		}
	}
}


void SceneLayer::cull_generate_cmds(void* job_input_ptr)
{
	auto* slice          = static_cast<CullJobSlice*>(job_input_ptr);
	const auto* cull_rig = slice->cull_rig;

	/* culling lambdas */

	auto push_cmd = [slice](uint32_t prim_idx_push)
	{
		const auto& prim = slice->scene_primitives[prim_idx_push];

		rdr::SceneDrawCmd cmd {};

		cmd.sort_key =
			(static_cast<uint64_t>(prim.material_idx      & 0xFFFFFFU) << 40) |
			(static_cast<uint64_t>(prim.submesh.vtx_base  & 0xFFFFU)   << 24) |
			(static_cast<uint64_t>(prim.submesh.idx_first & 0xFFFFFFU));

		cmd.layer_idx = slice->layer_idx;
		cmd.vtx_base  = prim.submesh.vtx_base;
		cmd.idx_first = prim.submesh.idx_first;
		cmd.idx_count = prim.submesh.idx_count;
		cmd.trs_idx   = slice->ecs_trs_idxs[prim_idx_push];
		cmd.mat_idx   = prim.material_idx;

		cmd.flags = (prim.entity == slice->selected_entity)
			? static_cast<uint8_t>(rdr::SceneDrawCmdFlag::selected) : 0;

		slice->async_result->push(cmd, prim_idx_push);
	};

	auto test_occlusion = [&push_cmd, slice, cull_rig](uint32_t prim_idx_cull)
	{
		const uint32_t occludee_idx = slice->occludee_idxs[prim_idx_cull];
		const auto& hull_slice       = cull_rig->hull_geoslices[occludee_idx];

		if (!hull_slice.is_valid()) {
			push_cmd(prim_idx_cull);
			return;
		}

		const mat4& mtx_M   = slice->matrices_M[prim_idx_cull];
		const mat4  mtx_MVP = slice->mtx_VP * mtx_M;

		float x_min_ndc  = std::numeric_limits<float>::max();
		float y_min_ndc  = std::numeric_limits<float>::max();
		float x_max_ndc  = std::numeric_limits<float>::lowest();
		float y_max_ndc  = std::numeric_limits<float>::lowest();
		float nearest_z  = 1.0f;
		float farthest_z = std::numeric_limits<float>::lowest();

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

			nearest_z  = std::min(nearest_z,  z_norm);
			farthest_z = std::max(farthest_z, z_norm);

			x_min_ndc  = std::min(x_min_ndc, pos_clip.x * w_inv);
			y_min_ndc  = std::min(y_min_ndc, pos_clip.y * w_inv);
			x_max_ndc  = std::max(x_max_ndc, pos_clip.x * w_inv);
			y_max_ndc  = std::max(y_max_ndc, pos_clip.y * w_inv);
		}

		if (crosses_near_plane) {
			push_cmd(prim_idx_cull);
			return;
		}

		++slice->async_result->occlusion_tested;

		const float w_px = (x_max_ndc - x_min_ndc) * 0.5f * static_cast<float>(HiZBuffer::width);
		const float h_px = (y_max_ndc - y_min_ndc) * 0.5f * static_cast<float>(HiZBuffer::height);

		int mip = std::clamp(static_cast<int>(std::log2(std::max({w_px, h_px, 1.0f}))), 0, 2);

		const int w_mip = static_cast<int>(HiZBuffer::width)  >> mip;
		const int h_mip = static_cast<int>(HiZBuffer::height) >> mip;

		int32_t x_min_full_px = static_cast<int32_t>(std::floor((x_min_ndc * 0.5f + 0.5f) * static_cast<float>(HiZBuffer::width)));
		int32_t x_max_full_px = static_cast<int32_t>(std::ceil((x_max_ndc  * 0.5f + 0.5f) * static_cast<float>(HiZBuffer::width)));
		int32_t y_min_full_px = static_cast<int32_t>(std::floor((y_min_ndc * 0.5f + 0.5f) * static_cast<float>(HiZBuffer::height)));
		int32_t y_max_full_px = static_cast<int32_t>(std::ceil((y_max_ndc  * 0.5f + 0.5f) * static_cast<float>(HiZBuffer::height)));

		int x_min_mip_px = std::clamp(x_min_full_px >> mip, 0, w_mip - 1);
		int x_max_mip_px = std::clamp(x_max_full_px >> mip, 0, w_mip - 1);
		int y_min_mip_px = std::clamp(y_min_full_px >> mip, 0, h_mip - 1);
		int y_max_mip_px = std::clamp(y_max_full_px >> mip, 0, h_mip - 1);

		float max_depth = std::numeric_limits<float>::lowest();

		for (int32_t y = y_min_mip_px; y <= y_max_mip_px; ++y) {

			int32_t row_offset = y * w_mip;

			for (int32_t x = x_min_mip_px; x <= x_max_mip_px; ++x) {
				max_depth =
					std::max(max_depth, slice->hiz_buffer->mips[mip][static_cast<size_t>(row_offset + x)]);
			}
		}

		if (nearest_z > max_depth) {
			++slice->async_result->occlusion_culled;
		}
		else {
			push_cmd(prim_idx_cull);
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

	/* vectorized frustum culling - main loop */

	uint32_t prim_idx = slice->begin;

	for (; prim_idx + 7 < slice->end; prim_idx += 8) {
		
		__m256 min_x = _mm256_loadu_ps(&slice->aabb_min_x[prim_idx]);
		__m256 min_y = _mm256_loadu_ps(&slice->aabb_min_y[prim_idx]);
		__m256 min_z = _mm256_loadu_ps(&slice->aabb_min_z[prim_idx]);

		__m256 max_x = _mm256_loadu_ps(&slice->aabb_max_x[prim_idx]);
		__m256 max_y = _mm256_loadu_ps(&slice->aabb_max_y[prim_idx]);
		__m256 max_z = _mm256_loadu_ps(&slice->aabb_max_z[prim_idx]);

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

			uint32_t curr_prim = prim_idx + active_lane_idx;
			test_occlusion(curr_prim);
		}
	}

	/* scalar frustum culling - tail loop */

	for (; prim_idx < slice->end; ++prim_idx) {

		const float center_x   = (slice->aabb_min_x[prim_idx] + slice->aabb_max_x[prim_idx]) * 0.5f;
		const float center_y   = (slice->aabb_min_y[prim_idx] + slice->aabb_max_y[prim_idx]) * 0.5f;
		const float center_z   = (slice->aabb_min_z[prim_idx] + slice->aabb_max_z[prim_idx]) * 0.5f;
		const float half_ext_x = (slice->aabb_max_x[prim_idx] - slice->aabb_min_x[prim_idx]) * 0.5f;
		const float half_ext_y = (slice->aabb_max_y[prim_idx] - slice->aabb_min_y[prim_idx]) * 0.5f;
		const float half_ext_z = (slice->aabb_max_z[prim_idx] - slice->aabb_min_z[prim_idx]) * 0.5f;

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

		test_occlusion(prim_idx);
	}
}


scn::RayHit SceneLayer::raycast_scene(
	const scn::Ray&            ray,
	const scn::Scene&          scene,
	const rdr::StagingContext& staging_ctx
)
{
	const uint32_t visible_count =
		static_cast<uint32_t>(scene.render_rig.prims_visible.size());

	if (visible_count == 0) {
		return scn::RayHit {};
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

	const uint32_t slice_count =
		(visible_count + cfg::job_grain - 1) / cfg::job_grain;

	mtp::slag<scn::RaycastJobSlice, mtp::default_set> slices(slice_count, scn::RaycastJobSlice{});

	mtp::slag<scn::RaycastAsyncResult, mtp_scn_set> async_results(
		m_metapool,
		slice_count,
		scn::RaycastAsyncResult {}
	);

	for (uint32_t slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
		auto& slice = slices[slice_idx];

		slice.begin = slice_idx * cfg::job_grain;
		slice.end   = std::min(slice.begin + cfg::job_grain, static_cast<uint32_t>(visible_count));

		slice.ray           = ray;
		slice.inv_direction = dir_inv;
		slice.rig           = &scene.render_rig;
		slice.staging_ctx   = &staging_ctx;
		slice.async_result  = &async_results[slice_idx];
	}

	auto ray_start_time = std::chrono::steady_clock::now();

	job::JobLatch job_latch;

	m_job_scheduler.dispatch_range(
		job_latch,
		&SceneLayer::raycast,
		slice_count,
		1,
		slices.data()
	);

	job_latch.wait();

	auto ray_end_time = std::chrono::steady_clock::now();

	m_harvester.scene_layer_curr().ray_job_ms =
		std::chrono::duration<double, std::milli>(ray_end_time - ray_start_time).count();

	scn::RayHit scene_hit {};

	for (uint32_t i = 0; i < slice_count; ++i) {
		const scn::RayHit& job_slice_hit = async_results[i].ray_hit;
		
		if (job_slice_hit.is_hit && job_slice_hit.closest_hit_dist < scene_hit.closest_hit_dist) {
			scene_hit = job_slice_hit;
		}
	}

	return scene_hit;
}


void SceneLayer::raycast(void* job_input_ptr)
{
	auto* slice = static_cast<scn::RaycastJobSlice*>(job_input_ptr);

	scn::RayHit local_hit {};

	const vec3  ray_origin = slice->ray.origin;
	const vec3  ray_dir    = slice->ray.direction;
	const vec3  dir_inv    = slice->inv_direction;

	const rdr::SceneVertex* vtx_data = slice->staging_ctx->scn_vtx_mass->vtx_data();
	const uint32_t* idx_data         = slice->staging_ctx->scn_vtx_mass->idx_data();

	const auto& x_min_aabbs   = slice->rig->aabb_world_min_x;
	const auto& y_min_aabbs   = slice->rig->aabb_world_min_y;
	const auto& z_min_aabbs   = slice->rig->aabb_world_min_z;
	const auto& x_max_aabbs   = slice->rig->aabb_world_max_x;
	const auto& y_max_aabbs   = slice->rig->aabb_world_max_y;
	const auto& z_max_aabbs   = slice->rig->aabb_world_max_z;

	const auto& primitives    = slice->rig->primitives;
	const auto& ecs_trs_idx   = slice->rig->ecs_trs_idxs;
	const auto& prims_visible = slice->rig->prims_visible;

	const __m256 x_origin  = _mm256_set1_ps(ray_origin.x);
	const __m256 y_origin  = _mm256_set1_ps(ray_origin.y);
	const __m256 z_origin  = _mm256_set1_ps(ray_origin.z);

	const __m256 x_dir_inv = _mm256_set1_ps(dir_inv.x);
	const __m256 y_dir_inv = _mm256_set1_ps(dir_inv.y);
	const __m256 z_dir_inv = _mm256_set1_ps(dir_inv.z);

	const __m256 zero = _mm256_setzero_ps();

	uint32_t prim_idx = slice->begin;

	/* vectorized raycast - main loop */

	for (; prim_idx + 7 < slice->end; prim_idx += 8) {

		__m256i loaded_indices = _mm256_loadu_si256(
			reinterpret_cast<const __m256i*>(&prims_visible[prim_idx])
		);

		__m256 x_min      = _mm256_i32gather_ps(x_min_aabbs.data(), loaded_indices, 4);
		__m256 y_min      = _mm256_i32gather_ps(y_min_aabbs.data(), loaded_indices, 4);
		__m256 z_min      = _mm256_i32gather_ps(z_min_aabbs.data(), loaded_indices, 4);

		__m256 x_max      = _mm256_i32gather_ps(x_max_aabbs.data(), loaded_indices, 4);
		__m256 y_max      = _mm256_i32gather_ps(y_max_aabbs.data(), loaded_indices, 4);
		__m256 z_max      = _mm256_i32gather_ps(z_max_aabbs.data(), loaded_indices, 4);

		__m256 x_min_dist = _mm256_mul_ps(_mm256_sub_ps(x_min, x_origin), x_dir_inv);
		__m256 x_max_dist = _mm256_mul_ps(_mm256_sub_ps(x_max, x_origin), x_dir_inv);
		__m256 x_entry    = _mm256_min_ps(x_min_dist, x_max_dist);
		__m256 x_exit     = _mm256_max_ps(x_min_dist, x_max_dist);

		__m256 y_min_dist = _mm256_mul_ps(_mm256_sub_ps(y_min, y_origin), y_dir_inv);
		__m256 y_max_dist = _mm256_mul_ps(_mm256_sub_ps(y_max, y_origin), y_dir_inv);
		__m256 y_entry    = _mm256_min_ps(y_min_dist, y_max_dist);
		__m256 y_exit     = _mm256_max_ps(y_min_dist, y_max_dist);

		__m256 z_min_dist = _mm256_mul_ps(_mm256_sub_ps(z_min, z_origin), z_dir_inv);
		__m256 z_max_dist = _mm256_mul_ps(_mm256_sub_ps(z_max, z_origin), z_dir_inv);
		__m256 z_entry    = _mm256_min_ps(z_min_dist, z_max_dist);
		__m256 z_exit     = _mm256_max_ps(z_min_dist, z_max_dist);

		__m256 aabb_entry = _mm256_max_ps(_mm256_max_ps(x_entry, y_entry), z_entry);
		__m256 aabb_exit  = _mm256_min_ps(_mm256_min_ps(x_exit,  y_exit),  z_exit);

		__m256 hit_dist_min    = _mm256_set1_ps(local_hit.closest_hit_dist);
		__m256 mask_valid_span = _mm256_cmp_ps(aabb_exit,  aabb_entry,   _CMP_GE_OQ);
		__m256 mask_in_front   = _mm256_cmp_ps(aabb_exit,  zero,         _CMP_GE_OQ);
		__m256 mask_closer     = _mm256_cmp_ps(aabb_entry, hit_dist_min, _CMP_LT_OQ);

		__m256 hit_mask =
			_mm256_and_ps(_mm256_and_ps(mask_valid_span, mask_in_front), mask_closer);

		uint32_t active_lane_mask =
			static_cast<uint32_t>(_mm256_movemask_ps(hit_mask));

		while (active_lane_mask) {

			uint32_t active_lane_index =
				static_cast<uint32_t>(std::countr_zero(active_lane_mask));

			active_lane_mask &= (active_lane_mask - 1);

			const uint32_t visible_index = prims_visible[prim_idx + active_lane_index];
			const auto& primitive        = primitives[visible_index];
			const auto& submesh          = primitive.submesh;

			const mat4 mtx_M =
				slice->staging_ctx->scn_trs_mass->get_raw(ecs_trs_idx[visible_index]).mtx_M;

			float closest_trig_dist = std::numeric_limits<float>::infinity();

			for (uint32_t idx_offset = 0; idx_offset < submesh.idx_count; idx_offset += 3) {
				
				uint32_t idx_0 = idx_data[submesh.idx_first + idx_offset + 0];
				uint32_t idx_1 = idx_data[submesh.idx_first + idx_offset + 1];
				uint32_t idx_2 = idx_data[submesh.idx_first + idx_offset + 2];

				const auto& vtx_0 = vtx_data[submesh.vtx_base + idx_0];
				const auto& vtx_1 = vtx_data[submesh.vtx_base + idx_1];
				const auto& vtx_2 = vtx_data[submesh.vtx_base + idx_2];

				vec3 vtx_W_0 = vec3(mtx_M * vec4(vtx_0.pos.x, vtx_0.pos.y, vtx_0.pos.z, 1.0f));
				vec3 vtx_W_1 = vec3(mtx_M * vec4(vtx_1.pos.x, vtx_1.pos.y, vtx_1.pos.z, 1.0f));
				vec3 vtx_W_2 = vec3(mtx_M * vec4(vtx_2.pos.x, vtx_2.pos.y, vtx_2.pos.z, 1.0f));

				float bary_U, bary_V, trig_hit_dist;

				if (scn::intersect_ray_triangle(
					ray_origin,
					ray_dir,
					vtx_W_0,
					vtx_W_1,
					vtx_W_2,
					trig_hit_dist,
					bary_U,
					bary_V
				)) {
					if (trig_hit_dist < closest_trig_dist) {
						closest_trig_dist = trig_hit_dist;
					}
				}
			}

			if (closest_trig_dist < local_hit.closest_hit_dist) {
				local_hit.is_hit           = true;
				local_hit.entity           = primitive.entity;
				local_hit.submesh          = visible_index;
				local_hit.closest_hit_dist = closest_trig_dist;
			}
		}
	}

	/* scalar raycast - tail loop */

	for (; prim_idx < slice->end; ++prim_idx) {

		const uint32_t visible_idx = prims_visible[prim_idx];

		float x_min_dist = (x_min_aabbs[visible_idx] - ray_origin.x) * dir_inv.x;
		float x_max_dist = (x_max_aabbs[visible_idx] - ray_origin.x) * dir_inv.x;
		float aabb_entry = std::min(x_min_dist, x_max_dist);
		float aabb_exit  = std::max(x_min_dist, x_max_dist);

		float y_min_dist = (y_min_aabbs[visible_idx] - ray_origin.y) * dir_inv.y;
		float y_max_dist = (y_max_aabbs[visible_idx] - ray_origin.y) * dir_inv.y;
		float y_entry    = std::min(y_min_dist, y_max_dist);
		float y_exit     = std::max(y_min_dist, y_max_dist);
		aabb_entry       = std::max(aabb_entry, y_entry);
		aabb_exit        = std::min(aabb_exit,  y_exit);

		float z_min_dist = (z_min_aabbs[visible_idx] - ray_origin.z) * dir_inv.z;
		float z_max_dist = (z_max_aabbs[visible_idx] - ray_origin.z) * dir_inv.z;
		float z_entry    = std::min(z_min_dist, z_max_dist);
		float z_exit     = std::max(z_min_dist, z_max_dist);
		aabb_entry       = std::max(aabb_entry, z_entry);
		aabb_exit        = std::min(aabb_exit,  z_exit);

		if (aabb_exit >= aabb_entry && aabb_exit >= 0.0f && aabb_entry < local_hit.closest_hit_dist) {

			const auto& primitive = primitives[visible_idx];
			const mat4  mtx_M     = slice->staging_ctx->scn_trs_mass->get_raw(ecs_trs_idx[visible_idx]).mtx_M;
			const auto& submesh   = primitive.submesh;

			float closest_trig_dist = std::numeric_limits<float>::infinity();

			for (uint32_t idx_offset = 0; idx_offset < submesh.idx_count; idx_offset += 3) {
				
				uint32_t idx_0 = idx_data[submesh.idx_first + idx_offset + 0];
				uint32_t idx_1 = idx_data[submesh.idx_first + idx_offset + 1];
				uint32_t idx_2 = idx_data[submesh.idx_first + idx_offset + 2];

				const auto& vtx_0 = vtx_data[submesh.vtx_base + idx_0];
				const auto& vtx_1 = vtx_data[submesh.vtx_base + idx_1];
				const auto& vtx_2 = vtx_data[submesh.vtx_base + idx_2];

				vec3 vtx_W_0 = vec3(mtx_M * vec4(vtx_0.pos.x, vtx_0.pos.y, vtx_0.pos.z, 1.0f));
				vec3 vtx_W_1 = vec3(mtx_M * vec4(vtx_1.pos.x, vtx_1.pos.y, vtx_1.pos.z, 1.0f));
				vec3 vtx_W_2 = vec3(mtx_M * vec4(vtx_2.pos.x, vtx_2.pos.y, vtx_2.pos.z, 1.0f));

				float barycentric_U, barycentric_V, trig_hit_dist;

				if (scn::intersect_ray_triangle(
					ray_origin,
					ray_dir,
					vtx_W_0,
					vtx_W_1,
					vtx_W_2,
					trig_hit_dist,
					barycentric_U,
					barycentric_V
				)) {
					if (trig_hit_dist < closest_trig_dist) {
						closest_trig_dist = trig_hit_dist;
					}
				}
			}

			if (closest_trig_dist < local_hit.closest_hit_dist) {
				local_hit.is_hit           = true;
				local_hit.entity           = primitive.entity;
				local_hit.submesh          = visible_idx;
				local_hit.closest_hit_dist = closest_trig_dist;
			}
		}
	}

	slice->async_result->ray_hit = local_hit;
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

} // hpr

