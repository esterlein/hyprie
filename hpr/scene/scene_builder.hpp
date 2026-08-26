#pragma once

#include "scene_io.hpp"
#include "scene_rig.hpp"
#include "scene_core.hpp"
#include "scene_data.hpp"

#include "asset_keeper.hpp"
#include "render_forge.hpp"

#include "ecs_registry_types.hpp"


namespace hpr::scn {


class SceneBuilder
{
public:

	SceneBuilder(
		MainRegistry&     registry,
		res::AssetKeeper& asset_keeper,
		rdr::RenderForge& render_forge
	)
		: m_registry     {registry}
		, m_asset_keeper {asset_keeper}
		, m_render_forge {render_forge}
	{}

	scn::Scene build(const char* uri)
	{
		scn::io::SceneDoc scene_doc {};
		if (!scn::io::load_scene(uri, scene_doc)){
			HPR_FATAL(
				log::LogCategory::scene,
				"[scene][build] read scene file failed"
			);
			return scn::Scene {};
		}

		scn::Scene scene {};
		if (!scn::instantiate_scene(
			scene_doc,
			m_registry,
			m_asset_keeper,
			m_render_forge,
			scene
		)) {
			HPR_FATAL(
				log::LogCategory::scene,
				"[scene][build] instantiate scene failed"
			);
			return scene;
		}

		return scene;
	}

private:

	MainRegistry&     m_registry;
	res::AssetKeeper& m_asset_keeper;
	rdr::RenderForge& m_render_forge;

	static constexpr const char* m_scene_uri {"scene://dragons.toml"};
};

} // hpr::scn
