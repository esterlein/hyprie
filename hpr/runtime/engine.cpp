
#include "engine.hpp"

#include "editor.hpp"
#include "canonical.hpp"
#include "render_data.hpp"

#include "surface.hpp"
#include "event_adapter.hpp"

#include "fx_layer.hpp"
#include "edit_layer.hpp"
#include "gizmo_layer.hpp"
#include "stage_layer.hpp"
#include "debug_layer.hpp"

#include "tracy/Tracy.hpp"

#include <memory>


namespace hpr {


Engine::Engine()
	: m_render_hub      {}
	, m_renderer        {m_render_hub, rdr::query_surface_info(), m_stats_harvester}
	, m_asset_keeper    {}
	, m_forge_resolver  {
		m_asset_keeper.storage<res::ImageResource>(),
		m_asset_keeper.storage<res::MaterialResource>()
	}
	, m_render_forge {
		m_render_hub,
		m_forge_resolver,
		rdr::query_surface_info()
	}
	, m_scene_builder   {m_ecs_registry, m_asset_keeper, m_render_forge}
	, m_layer_stack     {m_ecs_registry, rdr::query_surface_info()}
	, m_ui_backend      {m_render_forge, m_ui_resolver}
	, m_ui_resolver     {m_render_hub.storage<rdr::Texture>()}
	, m_editor_resolver {
		m_render_hub.storage<rdr::MaterialInstance>(),
		m_render_hub.storage<rdr::MaterialTemplate>()
	}
	, m_input_backend   {}
	, m_input_binding   {}
	, m_input_mapper    {m_input_binding}
	, m_font_mason      {m_asset_keeper, m_render_forge}
{
	m_render_forge.create_tilemap(scn::cfg::chunk_size, scn::cfg::chunk_size);

	m_input_backend.input_state = &m_input_state;
	m_ui_backend.rebuild_default_font(rdr::query_surface_info().dpi);
	m_font_mason.install_debug_fonts();
}


void Engine::init()
{
	rdr::SurfaceInfo surface_info = rdr::query_surface_info();

	auto canonical_shapes =
		geo::create_canonical_shapes(m_render_forge, m_render_hub);

	scn::Scene scene = m_scene_builder.build("scene://dragons.toml");

	m_render_forge.gpu_sync();

	m_renderer.set_render_context(
		m_render_forge.binding_context(),
		m_render_forge.staging_context(),
		canonical_shapes
	);

	m_layer_stack.init();

	m_layer_stack.push_layer(std::make_unique<lyr::SceneLayer>(
		std::move(scene),
		m_ecs_registry,
		m_scn_metapool,
		surface_info,
		m_render_forge.staging_context(),
		canonical_shapes,
		m_renderer.scene_queue(),
		m_renderer.anim_queue(),
		m_renderer.cue_queue(),
		m_renderer.overlay_queue(),
		m_stats_harvester
	));

	m_layer_stack.push_overlay(std::make_unique<lyr::FxLayer>(
		edt::GridParams {
			.minor_rgb          = vec3(0.5f, 0.5f, 0.5f),
			.major_rgb          = vec3(1.0f, 1.0f, 1.0f),
			.minor_vis_range_px = vec2(2.0f, 8.0f),
			.major_vis_range_px = vec2(4.0f, 16.0f),
			.line_width_px      = 1.0f,
			.cell_size          = 1.0f,
			.y_plane            = 0.0f,
			.major_step_cells   = 8
		},
		m_renderer.fx_queue()
	));

	m_layer_stack.push_overlay(std::make_unique<lyr::StageLayer>(
		canonical_shapes,
		m_ecs_registry,
		surface_info,
		m_render_forge.staging_context(),
		m_renderer.cue_queue(),
		m_renderer.overlay_queue()
	));

	m_layer_stack.push_overlay(std::make_unique<lyr::GizmoLayer>(
		edt::create_gizmo_primitives(m_render_forge, m_render_hub),
		surface_info,
		m_render_forge.staging_context(),
		m_renderer.overlay_queue()
	));

	m_layer_stack.push_overlay(std::make_unique<lyr::EditLayer>(
		m_input_state,
		m_ui_backend,
		surface_info,
		m_editor_resolver,
		m_renderer.ui_queue()
	));

	m_layer_stack.push_overlay(std::make_unique<lyr::DebugLayer>(
		m_font_mason.debug_fonts(),
		lyr::FontResolver {
			m_render_hub.storage<rdr::Font>()
		},
		surface_info,
		m_renderer.debug_queue(),
		m_renderer,
		m_stats_harvester
	));

	m_render_forge.gpu_sync();
}


void Engine::frame(float delta_time)
{
	ZoneScoped;

	m_stats_harvester.begin_frame();

	m_actions.clear();
	m_input_mapper.map(m_input_state, m_actions);
	m_input_state.clear_mouse_delta();

	const std::span<const io::Action> actions_span {m_actions.data(), m_actions.size()};
	m_layer_stack.on_actions(actions_span);
	m_layer_stack.on_update(m_renderer, delta_time);
	m_layer_stack.on_submit();

	m_renderer.frame();

	m_input_state.clear_ui_frame();

	FrameMark;
}


void Engine::on_event(const sapp_event* event)
{
	m_input_backend.on_event(event);

	if (std::unique_ptr<Event> hpr_event = EventAdapter::adapt(event)) {
		m_renderer.on_event(*hpr_event);
		m_render_forge.on_event(*hpr_event);
		m_layer_stack.on_event(*hpr_event);
	}
}


void Engine::shutdown()
{
	m_renderer.shutdown();
}

} // hpr

