#include <memory>

#include "engine.hpp"
#include "editor.hpp"
#include "render_data.hpp"

#include "scene_layer.hpp"
#include "edit_layer.hpp"
#include "gizmo_layer.hpp"
#include "fx_layer.hpp"
#include "debug_layer.hpp"


namespace hpr {


Engine::Engine()
	: m_render_hub      {}
	, m_renderer        {m_render_hub}
	, m_asset_keeper    {}
	, m_forge_resolver  {
		m_asset_keeper.storage<res::ImageResource>(),
		m_asset_keeper.storage<res::MaterialResource>()
	}
	, m_render_forge    {m_render_hub, m_forge_resolver, m_renderer.surface_info()}
	, m_ui_backend      {m_render_forge, m_ui_resolver}
	, m_ui_resolver     {m_render_hub.storage<rdr::FontTexture>()}
	, m_scene_resolver  {
		m_render_hub.storage<rdr::Mesh>(),
		m_render_hub.storage<rdr::MeshGeometry>(),
		m_render_hub.storage<rdr::MaterialInstance>()
	}
	, m_editor_resolver {
		m_render_hub.storage<rdr::MaterialInstance>(),
		m_render_hub.storage<rdr::MaterialTemplate>()
	}
	, m_input_backend   {}
	, m_input_binding   {}
	, m_input_mapper    {m_input_binding}
{
	m_input_backend.input_state = &m_input_state;
	m_renderer.set_programs(m_render_forge.get_render_programs());
}

void Engine::init()
{
	m_layer_stack.push_layer(std::make_unique<SceneLayer>(
		m_ecs_registry,
		m_asset_keeper,
		m_render_forge,
		m_renderer,
		m_input_binding,
		m_scene_resolver,
		m_scene_path
	));

	m_layer_stack.push_overlay(std::make_unique<FxLayer>(
		m_renderer,
		edt::GridParams {
			.color_minor_rgb = vec3(0.5f, 0.5f, 0.5f),
			.base_spacing    = 1.0f,
			.color_major_rgb = vec3(1.0f, 1.0f, 1.0f),
			.target_px       = 32.0f,
			.line_width_px   = 1.0f,
			.major_step      = 8,
		}
	));

	m_layer_stack.push_overlay(std::make_unique<GizmoLayer>(
		m_renderer,
		edt::create_gizmo_primitives(m_render_forge)
	));

	m_layer_stack.push_overlay(std::make_unique<EditLayer>(
		m_renderer,
		m_input_state,
		m_ui_backend,
		m_editor_resolver
	));

	m_layer_stack.push_overlay(std::make_unique<DebugLayer>(
		m_renderer,
		m_ui_backend
	));
}

void Engine::frame(float delta_time)
{
	m_actions.clear();
	m_input_mapper.map(m_input_state, m_actions);
	m_input_state.clear_mouse_delta();

	const std::span<const Action> actions_span {m_actions.data(), m_actions.size()};
	m_layer_stack.on_actions(actions_span);
	m_layer_stack.on_update(delta_time);
	m_layer_stack.on_submit(m_renderer);

	m_renderer.frame();

	m_input_state.clear_ui_frame();
}

void Engine::on_event(const sapp_event* event)
{
	m_input_backend.on_event(event);
	m_renderer.handle_event(event);
	m_layer_stack.on_event(event);
}

void Engine::shutdown()
{
	m_renderer.shutdown();
}

} // hpr

