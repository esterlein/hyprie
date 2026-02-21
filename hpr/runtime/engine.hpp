#pragma once

#include <span>
#include <memory>

#include "renderer.hpp"
#include "render_hub.hpp"
#include "render_forge.hpp"
#include "asset_keeper.hpp"

#include "ecs_registry.hpp"

#include "input_backend.hpp"
#include "input_state.hpp"
#include "input_binding.hpp"
#include "input_mapper.hpp"

#include "components_render.hpp"
#include "components_scene.hpp"

#include "action.hpp"

#include "scene_resolver.hpp"
#include "editor_resolver.hpp"

#include "layer_stack.hpp"
#include "ui_resolver.hpp"
#include "ui_backend.hpp"

#include "scene_layer.hpp"


namespace hpr {


class Engine
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

	Engine();

	void init();
	void frame(float delta_time);
	void on_event(const sapp_event* event);
	void shutdown();

	void update() {}
	void tick() {}

private:

	mtp::shared<SceneLayer::mtp_scn_set> m_scn_metapool;

	rdr::RenderHub     m_render_hub;
	rdr::Renderer      m_renderer;
	res::AssetKeeper   m_asset_keeper;
	rdr::ForgeResolver m_forge_resolver;
	rdr::RenderForge   m_render_forge;

	ui::UiBackend       m_ui_backend;
	ui::UiResolver      m_ui_resolver;

	scn::SceneResolver  m_scene_resolver;
	edt::EditorResolver m_editor_resolver;

	io::InputBackend   m_input_backend;
	io::InputState     m_input_state   {};
	io::InputBinding   m_input_binding {};
	io::InputMapper    m_input_mapper;

	mtp::vault<Action, mtp::default_set> m_actions {};

	ECSRegistry m_ecs_registry;

	LayerStack m_layer_stack;

	const char* m_scene_path = "../test_scene/scene.toml";
};

} // hpr

