#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "layer.hpp"
#include "stats.hpp"
#include "action.hpp"
#include "surface.hpp"

#include "render_queue.hpp"
#include "render_context.hpp"
#include "draw_queue_data.hpp"

#include "event_emitter.hpp"
#include "ecs_registry_types.hpp"

#include "scene_rig.hpp"
#include "scene_data.hpp"
#include "raycast_data.hpp"
#include "scene_context.hpp"

#include "scheduler.hpp"
#include "canonical_data.hpp"
#include "inspector_state.hpp"

#include <span>


namespace hpr::lyr {


namespace cfg {

inline constexpr uint32_t job_grain       = 64U;
inline constexpr uint32_t max_num_scenes  = 8U;
inline constexpr uint32_t max_scene_prims = 1'000'000U;

inline constexpr uint32_t max_depth_blas = 1U;
inline constexpr uint32_t max_depth_tlas = 7U;
inline constexpr uint32_t bvh_stack_size = 64U;

inline constexpr size_t hiz_mips_num = 3U;

} // hpr::lyr::cfg


class SceneLayer : public Layer, public EventEmitter, public edt::InspectorProvider
{
private:

	struct FrustumPlane
	{
		vec3  nrm;
		vec3  nrm_abs;
		float offset;
	};

	struct HiZBuffer
	{
		static constexpr uint32_t width  {256U};
		static constexpr uint32_t height {128U};

		mtp::vault<float, mtp::default_set> mips[cfg::hiz_mips_num];
	};

	template <uint32_t Capacity>
	struct CmdAsyncResult_t
	{
		std::array<uint32_t, Capacity> sbms_visible;
		uint32_t count = 0;

		uint32_t frustum_tested   = 0;
		uint32_t frustum_culled   = 0;
		uint32_t occlusion_tested = 0;
		uint32_t occlusion_culled = 0;

		void clear()
		{
			count = 0;
		}

		void push(uint32_t sbm_idx)
		{
			HPR_ASSERT_MSG(count < Capacity,
				"draw command scratch overflow");

			sbms_visible[count] = sbm_idx;
			++count;
		}
	};

	using CullAsyncResult = CmdAsyncResult_t<cfg::job_grain>;

	struct CullJobSlice
	{
		uint32_t begin;
		uint32_t end;

		const mat4* mtxs_M;

		const float* aabb_min_x;
		const float* aabb_min_y;
		const float* aabb_min_z;
		const float* aabb_max_x;
		const float* aabb_max_y;
		const float* aabb_max_z;

		const FrustumPlane* frustum_planes;

		const scn::SceneCullRig* cull_rig;
		const uint32_t*          occludee_idxs;

		mat4 mtx_VP;

		const HiZBuffer* hiz_buffer;
		CullAsyncResult* async_result;
	};

public:

	static constexpr uint32_t mtp_scn_max_stride =
		((sizeof(CullAsyncResult) *
		cfg::max_scene_prims      /
		cfg::job_grain            +
		sizeof(size_t))           + 7U) & ~7U;

	using mtp_scn_set = mtp::metaset <
		mtp::def <
			mtp::capf::flat,
			cfg::max_num_scenes,
			8,
			mtp_scn_max_stride,
			mtp_scn_max_stride
		>
	>;

public:

	SceneLayer(
		scn::Scene                             scene,
		MainRegistry&                          ecs_registry,
		mtp::shared<mtp_scn_set>&              metapool,
		rdr::SurfaceInfo                       surface_info,
		rdr::StagingContext                    staging_ctx,
		geo::CanonicalShapes                   canonical_prims,
		rdr::RenderQueue<rdr::SceneDrawCmd>&   scene_queue,
		rdr::RenderQueue<rdr::AnimDrawCmd>&    anim_queue,
		rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
		rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue,
		log::StatsHarvester&                   harvester
	);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const io::Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void on_result(Event& event) override;
	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void process_commands(CmdStream::Reader reader) override;

	edt::InspectorSnapshot selection_properties() const override;

private:

	void process_raycast();
	void clear_and_resize();

	void sync_static_models();
	void sync_skinned_models(const scn::SceneContext& scene_ctx);
	void sync_occluder_twins(const scn::SceneContext& scene_ctx);

	void rasterize_hiz();
	void dispatch_culling(const scn::SceneContext& scene_ctx);

	void build_bvh_tlas();

	void submit_static_geometry(uint32_t layer_idx);
	void submit_skinned_geometry(uint32_t layer_idx);
	void submit_debug_wires(uint32_t layer_idx);

	static void cull(void* job_input_ptr);

private:

	HiZBuffer m_hiz_buffer;

private:

	scn::Scene                m_scene_rig;
	MainRegistry&             m_registry;
	mtp::shared<mtp_scn_set>& m_metapool;
	rdr::SurfaceInfo          m_surface_info;
	rdr::StagingContext       m_staging_ctx;

	geo::CanonicalShapes  m_canonical {};

	EventQueue* m_event_queue;

	job::Scheduler m_job_scheduler;

	scn::PickRayCtx m_pick_ctx  {};
	scn::Selection  m_selection {};

	rdr::RenderQueue<rdr::SceneDrawCmd>&   m_scene_queue;
	rdr::RenderQueue<rdr::AnimDrawCmd>&    m_anim_queue;
	rdr::RenderQueue<rdr::CueDrawCmd>&     m_cue_queue;
	rdr::RenderQueue<rdr::OverlayDrawCmd>& m_overlay_queue;

	log::StatsHarvester& m_harvester;

	bool m_show_cull {false};
	bool m_show_bvh  {false};
};

} // hpr::lyr

