#pragma once

#include <stdint.h>

#include "mtp_memory.hpp"

#include "handle.hpp"
#include "scene_data.hpp"
#include "asset_data.hpp"
#include "ecs_registry.hpp"
#include "scene_sim_data.hpp"
#include "tile_data.hpp"


namespace hpr::scn {


class Scene
{
public:

	Scene() = default;
	~Scene() = default;

	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;

	Scene(Scene&&) noexcept = default;
	Scene& operator=(Scene&&) noexcept = default;

public:

	using GuidMap     = decltype(mtp::make_unordered_map<uint64_t, ecs::Entity, mtp::default_set>());
	using MaterialMap = decltype(mtp::make_unordered_map<uint32_t, Handle<res::MaterialResource>, mtp::default_set>());

	void clear();

	void index(ecs::Entity entity, uint64_t guid);
	ecs::Entity find(uint64_t guid) const;
	const GuidMap& map() const;

	void set_ambient(vec3 rgb) noexcept;
	vec3 ambient() const noexcept;

	void bind_material_template(uint32_t id, Handle<res::MaterialResource> handle);
	Handle<res::MaterialResource> resolve_material_template(uint32_t id) const;


	StoreyStackSpec& add_storey_stack()
	{
		m_sim_data.storey_stack_specs.emplace_back(StoreyStackSpec {});
		return m_sim_data.storey_stack_specs.back();
	}

	void add_storey_stack(StoreyStackSpec&& stack_spec)
	{
		m_sim_data.storey_stack_specs.emplace_back(std::move(stack_spec));
	}

	void rebuild_stratum()
	{
		m_sim_data.stratum.rebuild(m_sim_data.storey_stack_specs);
	}


	mtp::vault<ScenePrimitive, mtp::default_set>& scene_primitives()
	{ return m_scene_primitives; }

	const mtp::vault<ScenePrimitive, mtp::default_set>& scene_primitives() const
	{ return m_scene_primitives; }

	Stratum& stratum()
	{ return m_sim_data.stratum; }

	const Stratum& stratum() const
	{ return m_sim_data.stratum; }

	TileField& tilefield()
	{ return m_sim_data.tilefield; }

	const TileField& tilefield() const
	{ return m_sim_data.tilefield; }

	TileGridParams& grid_params()
	{ return m_sim_data.grid_params; }

	const TileGridParams& grid_params() const
	{ return m_sim_data.grid_params; }

	rdr::TileChunkDrawableSet& tile_chunk_drawable_set()
	{ return m_sim_data.draw_data; }

	const rdr::TileChunkDrawableSet& tile_draw_data() const
	{ return m_sim_data.draw_data; }

private:

	vec3 m_ambient_rgb {0.0f, 0.0f, 0.0f};

	GuidMap m_guidmap {mtp::make_unordered_map<uint64_t, ecs::Entity, mtp::default_set>()};

	MaterialMap m_mat_templates {mtp::make_unordered_map<uint32_t, Handle<res::MaterialResource>, mtp::default_set>()};

	SceneSimData m_sim_data;

	mtp::vault<ScenePrimitive, mtp::default_set> m_scene_primitives;
};


} // hpr::scn

