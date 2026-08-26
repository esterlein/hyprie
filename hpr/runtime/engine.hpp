#pragma once

#include "stats.hpp"
#include "renderer.hpp"
#include "render_hub.hpp"
#include "render_forge.hpp"
#include "asset_keeper.hpp"

#include "ecs_registry.hpp"
#include "ecs_registry_types.hpp"

#include "input_backend.hpp"
#include "input_state.hpp"
#include "input_binding.hpp"
#include "input_mapper.hpp"

#include "font_mason.hpp"

#include "components_render.hpp"
#include "components_scene.hpp"

#include "action.hpp"

#include "scene_builder.hpp"
#include "scene_resolver.hpp"
#include "editor_resolver.hpp"

#include "layer_stack.hpp"
#include "ui_resolver.hpp"
#include "ui_backend.hpp"

#include "scene_layer.hpp"

#include <span>
#include <memory>


namespace hpr {


class Engine
{
public:

	Engine();

	void init();
	void frame(float delta_time);
	void on_event(const sapp_event* event);
	void shutdown();

	void update() {}
	void tick()   {}

private:

	mtp::shared<lyr::SceneLayer::mtp_scn_set> m_scn_metapool;

	rdr::RenderHub     m_render_hub;
	rdr::Renderer      m_renderer;
	res::AssetKeeper   m_asset_keeper;
	rdr::ForgeResolver m_forge_resolver;
	rdr::RenderForge   m_render_forge;
	scn::SceneBuilder  m_scene_builder;
	lyr::LayerStack    m_layer_stack;

	ui::UiBackend  m_ui_backend;
	ui::UiResolver m_ui_resolver;

	edt::EditorResolver m_editor_resolver;

	io::InputBackend m_input_backend;
	io::InputState   m_input_state   {};
	io::InputBinding m_input_binding {};
	io::InputMapper  m_input_mapper;

	rdr::FontMason m_font_mason;

	mtp::vault<io::Action, mtp::default_set> m_actions {};

	MainRegistry m_ecs_registry;

	log::StatsHarvester m_stats_harvester {};
};

} // hpr

