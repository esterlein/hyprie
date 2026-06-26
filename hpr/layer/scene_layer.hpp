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

#include "scene.hpp"
#include "scene_data.hpp"
#include "raycast_data.hpp"
#include "scene_context.hpp"

#include "scheduler.hpp"
#include "canonical_data.hpp"
#include "inspector_state.hpp"

#include <span>


namespace hpr {


namespace cfg {

inline constexpr uint32_t job_grain       = 64U;
inline constexpr uint32_t max_num_scenes  = 8U;
inline constexpr uint32_t max_scene_prims = 1'000'000U;

} // hpr::cfg


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

		mtp::vault<float, mtp::default_set> mips[3];
	};

	template <uint32_t Capacity>
	struct CmdAsyncResult_t
	{
		std::array<rdr::SceneDrawCmd, Capacity> cmds;
		std::array<uint32_t,          Capacity> prims_visible;
		uint32_t count = 0;

		uint32_t frustum_tested   = 0;
		uint32_t frustum_culled   = 0;
		uint32_t occlusion_tested = 0;
		uint32_t occlusion_culled = 0;

		void clear()
		{
			count = 0;
		}

		void push(const rdr::SceneDrawCmd& cmd, uint32_t prim_idx)
		{
			HPR_ASSERT_MSG(count < Capacity,
				"draw command scratch overflow");

			cmds[count] = cmd;
			prims_visible[count] = prim_idx;
			++count;
		}

		auto begin()
		{
			return cmds.begin();
		}

		auto end()
		{
			return cmds.begin() + count;
		}

		auto begin() const
		{
			return cmds.begin();
		}

		auto end() const
		{
			return cmds.begin() + count;
		}
	};

	using CmdAsyncResult = CmdAsyncResult_t<cfg::job_grain>;

	struct CullJobSlice
	{
		uint32_t begin;
		uint32_t end;

		const scn::ScenePrimitive* scene_primitives;
		const uint32_t*            ecs_trs_idxs;
		const mat4*                matrices_M;

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

		uint32_t    layer_idx;
		ecs::Entity selected_entity;

		CmdAsyncResult* async_result;
	};

public:

	static constexpr uint32_t mtp_scn_max_stride =
		((sizeof(CmdAsyncResult) *
		cfg::max_scene_prims     /
		cfg::job_grain           +
		sizeof(size_t))          + 7U) & ~7U;

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
		rdr::RenderQueue<rdr::SceneDrawCmd>&   scene_queue,
		rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
		rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue,
		log::StatsHarvester&                   harvester
	);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void on_result(Event& event) override;
	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void process_commands(CmdStream::Reader reader) override;

	scn::RayHit raycast_scene(
		const scn::Ray&            ray,
		const scn::Scene&          scene,
		const rdr::StagingContext& staging_ctx
	);

	edt::InspectorSnapshot selection_properties() const override;

private:

	static void cull_generate_cmds(void* job_input_ptr);
	static void raycast(void* job_input_ptr);

private:

	HiZBuffer m_hiz_buffer;

private:

	scn::Scene                m_scene;
	MainRegistry&             m_registry;
	mtp::shared<mtp_scn_set>& m_metapool;
	rdr::SurfaceInfo          m_surface_info;
	rdr::StagingContext       m_staging_ctx;

	geo::CanonicalPrimitives m_canonical {};

	EventQueue* m_event_queue;

	job::Scheduler m_job_scheduler;

	scn::Selection m_selection {};

	rdr::RenderQueue<rdr::SceneDrawCmd>&   m_scene_queue;
	rdr::RenderQueue<rdr::CueDrawCmd>&     m_cue_queue;
	rdr::RenderQueue<rdr::OverlayDrawCmd>& m_overlay_queue;

	log::StatsHarvester& m_harvester;

	bool m_show_cull_wireframes {false};
};

} // hpr

