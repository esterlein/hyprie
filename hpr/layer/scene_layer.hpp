#pragma once

#include <span>

#include "event_emitter.hpp"
#include "layer.hpp"
#include "action.hpp"

#include "ecs_registry.hpp"
#include "systems_scene.hpp"
#include "systems_render.hpp"

#include "scene.hpp"
#include "scene_data.hpp"

#include "camera.hpp"
#include "renderer.hpp"
#include "scheduler.hpp"
#include "render_forge.hpp"
#include "asset_keeper.hpp"
#include "input_binding.hpp"
#include "frame_context.hpp"
#include "scene_resolver.hpp"
#include "inspector_state.hpp"


namespace hpr {


class SceneLayer : public Layer, public EventEmitter, public edt::InspectorProvider
{
public:

	using ECSRegistry = ecs::Registry <
		ecs::TransformComponent,
		ecs::HierarchyComponent,
		ecs::NameComponent,
		ecs::ModelComponent,
		ecs::BoundComponent,
		ecs::CameraComponent,
		ecs::LightComponent
	>;

	SceneLayer(
		ECSRegistry&            ecs_registry,
		res::AssetKeeper&       asset_keeper,
		rdr::RenderForge&       render_forge,
		rdr::Renderer&          renderer,
		const io::InputBinding& input_binding,
		scn::SceneResolver&     resolver,
		const char*             scene_path
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
	scn::Camera m_camera_controller;
	EventQueue* m_event_queue;

	job::Scheduler m_scheduler;

	scn::Selection m_selection {};

	ecs::Entity m_active_cam_entity {ecs::invalid_entity};

	rdr::DrawView         m_draw_view {};
	rdr::DrawViewLightSet m_draw_view_light_set {};

	float m_orbit_dx {0.0f};
	float m_orbit_dy {0.0f};
	float m_pan_dx   {0.0f};
	float m_pan_dy   {0.0f};
	float m_dolly    {0.0f};

	float m_move_forward {0.0f};
	float m_move_right   {0.0f};
	float m_move_up      {0.0f};
};

} // hpr

