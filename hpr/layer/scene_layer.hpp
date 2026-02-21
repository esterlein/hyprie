#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include <span>

#include "layer.hpp"
#include "action.hpp"
#include "event_emitter.hpp"

#include "ecs_registry.hpp"
#include "systems_scene.hpp"
#include "systems_render.hpp"

#include "scene.hpp"
#include "scene_data.hpp"
#include "scene_resolver.hpp"

#include "renderer.hpp"
#include "scheduler.hpp"
#include "render_forge.hpp"
#include "asset_keeper.hpp"
#include "input_binding.hpp"
#include "frame_context.hpp"
#include "inspector_state.hpp"
#include "camera_controller.hpp"


namespace hpr {

namespace cfg {

inline constexpr uint32_t job_grain               = 64U;
inline constexpr uint32_t max_num_scenes          = 8U;
inline constexpr uint32_t max_models_per_scene    = 256U;
inline constexpr uint32_t max_submeshes_per_model = 32U;
inline constexpr uint32_t max_draw_cmds_per_slice = job_grain * max_submeshes_per_model;

}; // hpr::cfg


class SceneLayer : public Layer, public EventEmitter, public edt::InspectorProvider
{
private:

	template <uint32_t Capacity>
	struct DrawCmdAsyncResult_t
	{
		std::array<rdr::SceneDrawCommand, Capacity> cmds;
		uint32_t count = 0;

		void clear()
		{
			count = 0;
		}

		void push(const rdr::SceneDrawCommand& cmd)
		{
			HPR_ASSERT_MSG(count < Capacity,
				"draw command scratch overflow");
			cmds[count++] = cmd;
		}

		auto begin() { return cmds.begin(); }
		auto end() { return cmds.begin() + count; }

		auto begin() const { return cmds.begin(); }
		auto end() const { return cmds.begin() + count; }
	};


public:

	using DrawCmdAsyncResult = DrawCmdAsyncResult_t<cfg::max_draw_cmds_per_slice>;

	static constexpr uint32_t mtp_scn_max_stride =
		sizeof(DrawCmdAsyncResult) * cfg::max_models_per_scene / cfg::job_grain;

	using mtp_scn_set = mtp::metaset <

		mtp::def <
			mtp::capf::flat,
			cfg::max_num_scenes,
			8,
			mtp_scn_max_stride,
			mtp_scn_max_stride
		>
	>;

	using ECSRegistry = ecs::Registry <
		ecs::TransformComponent,
		ecs::HierarchyComponent,
		ecs::NameComponent,
		ecs::ModelComponent,
		ecs::BoundComponent,
		ecs::CameraComponent,
		ecs::LightComponent
	>;

public:

	SceneLayer(
		ECSRegistry&              ecs_registry,
		res::AssetKeeper&         asset_keeper,
		rdr::RenderForge&         render_forge,
		rdr::Renderer&            renderer,
		const io::InputBinding&   input_binding,
		scn::SceneResolver&       resolver,
		const char*               scene_path,
		mtp::shared<mtp_scn_set>& metapool
	);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;
	bool on_actions(std::span<const Action> actions) override;
	void on_update(float delta_time) override;
	void on_submit(rdr::Renderer& renderer, uint32_t layer_index) override;

	void on_result(Event& event) override;
	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	static void build_model_draw_cmds(void* job_input_ptr);

	void process_commands(CmdStream::Reader reader) override;

	edt::InspectorSnapshot selection_properties() const override;

private:

	ECSRegistry& m_registry;

	res::AssetKeeper& m_asset_keeper;
	rdr::RenderForge& m_render_forge;
	rdr::Renderer&    m_renderer;
	io::InputBinding  m_binding;

	scn::SceneResolver& m_resolver;

	const char* m_scene_path;

	scn::Scene  m_scene;
	EventQueue* m_event_queue;

	job::Scheduler m_job_scheduler;

	scn::Selection m_selection {};

	ecs::Entity m_active_cam_entity {ecs::invalid_entity};

	rdr::DrawView         m_draw_view {};
	rdr::DrawViewLightSet m_draw_view_light_set {};
	scn::CameraController m_cam_controller;

	mtp::slag<DrawCmdAsyncResult, mtp_scn_set> m_slice_draw_cmd_results;
};

} // hpr

