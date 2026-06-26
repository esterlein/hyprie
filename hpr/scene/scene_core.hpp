#pragma once

#include "hprint.hpp"

#include "mtp_memory.hpp"

#include "log.hpp"
#include "scene.hpp"
#include "asset_data.hpp"
#include "ecs_registry.hpp"
#include "asset_keeper.hpp"
#include "render_forge.hpp"
#include "scene_io_data.hpp"
#include "geometry_utils.hpp"
#include "components_scene.hpp"
#include "components_render.hpp"

#include <charconv>


namespace hpr::scn {


namespace cfg {

constexpr std::string_view hex_lower_prefix = "0x";
constexpr std::string_view hex_upper_prefix = "0X";

constexpr uint32_t hex_base     = 16;
constexpr uint64_t invalid_guid = 0;

} // hpr::scn::cfg


inline uint64_t parse_guid_hex(std::string_view guid_hex)
{
	if (guid_hex.starts_with(cfg::hex_lower_prefix) ||
		guid_hex.starts_with(cfg::hex_upper_prefix)) {
			guid_hex.remove_prefix(cfg::hex_lower_prefix.size());
	}

	uint64_t guid_value {};

	auto guid_result = std::from_chars(
		guid_hex.data(),
		guid_hex.data() + guid_hex.size(),
		guid_value,
		cfg::hex_base
	);

	return (guid_result.ec == std::errc {}) ? guid_value : cfg::invalid_guid;
}


template <typename... Components>
bool instantiate(
	const scn::io::SceneDoc&      scene_doc,
	ecs::Registry<Components...>& registry,
	res::AssetKeeper&             asset_keeper,
	rdr::RenderForge&             render_forge,
	scn::Scene&                   scene
)
{
	scene.clear_volatile();
	scene.render_rig.ambient_rgb = scene_doc.ambient_rgb;


	/* hardcoded tile layout */
	/*
	auto& stratum     = scene.stratum();
	auto& tilefield   = scene.tilefield();
	auto& grid_params = scene.grid_params();
	auto& tile_draw   = scene.tile_chunk_drawable_set();

	tile_draw.enabled    = true;
	tile_draw.storey_min = 0;
	tile_draw.storey_max = 2;

	tile_draw.tile_style = render_forge.create_tile_style();

	constexpr int32_t width  = 64;
	constexpr int32_t height = 64;
	constexpr int32_t floors = 3;

	tilefield.resize(width, height, floors, TileType {0});

	static constexpr int32_t storey_stack = 0;

	{
		StoreyStackSpec stack {};
		stack.stack_id          = storey_stack;
		stack.base_voxel_y      = 0;
		stack.base_storey_index = 0;

		stack.storey_specs.reserve(floors);

		stack.storey_specs.emplace_back(StoreySpec {.height_voxels = 5});
		stack.storey_specs.emplace_back(StoreySpec {.height_voxels = 4});
		stack.storey_specs.emplace_back(StoreySpec {.height_voxels = 5});

		scene.add_storey_stack(std::move(stack));
		scene.rebuild_stratum();
	}

	for (int32_t storey = 0; storey < floors; ++storey) {
		for (int32_t z = 0; z < 32; ++z) {
			for (int32_t x = 0; x < 32; ++x) {

				TileCoord tile_coord {x, z, storey, storey_stack};
				tilefield.set(tile_coord, TileType {2});
				scn::mark_dirty_chunk(stratum, grid_params, tile_coord, tile_draw);
			}
		}
	}
	*/

	/* make entities and parent links */

	auto guid_entity_map =
		mtp::make_unordered_map<uint64_t, ecs::Entity, mtp::default_set>();
	guid_entity_map.reserve(scene_doc.entity_docs.size());

	auto guid_children_map =
		mtp::make_unordered_map<uint64_t, mtp::vault<uint64_t, mtp::default_set>, mtp::default_set>();
	guid_children_map.reserve(scene_doc.entity_docs.size());

	std::unordered_map<Handle<res::ImportModel>, rdr::Model, HandleHasher> model_cache;

	for (const auto& entity_doc : scene_doc.entity_docs) {

		if (entity_doc.guid.empty()) {
			HPR_WARN(
				log::LogCategory::scene,
				"[instantiate] missing entity guid"
			);
			continue;
		}

		const uint64_t entity_guid = parse_guid_hex(entity_doc.guid);
		if (entity_guid == cfg::invalid_guid) {
			HPR_WARN(
				log::LogCategory::scene,
				"[instantiate] invalid entity guid [guid %s]",
				entity_doc.guid.c_str()
			);
			continue;
		}

		if (guid_entity_map.find(entity_guid) != guid_entity_map.end()) {
			HPR_WARN(
				log::LogCategory::scene,
				"[instantiate] duplicate entity guid [guid %llu]",
				static_cast<unsigned long long>(entity_guid)
			);
			continue;
		}

		ecs::Entity entity = registry.create_entity();

		guid_entity_map[entity_guid] = entity;

		if (!entity_doc.parent_guid.empty()) {
			const uint64_t parent_guid = parse_guid_hex(entity_doc.parent_guid);
			if (parent_guid == cfg::invalid_guid) {
				HPR_WARN(
					log::LogCategory::scene,
					"[instantiate] invalid parent guid [child %llu][parent %s]",
					static_cast<unsigned long long>(entity_guid),
					entity_doc.parent_guid.c_str()
				);
			}
			else {
				auto& children_list = guid_children_map[parent_guid];
				children_list.emplace_back(parent_guid);
				children_list.back() = entity_guid;
			}
		}
	}

	/* attach components */

	for (const auto& entity_doc : scene_doc.entity_docs) {

		const uint64_t entity_guid = parse_guid_hex(entity_doc.guid);
		auto entity_it = guid_entity_map.find(entity_guid);
		if (entity_it == guid_entity_map.end())
			continue;

		ecs::Entity entity = entity_it->second;

		if (!entity_doc.name.empty()) {
			ecs::NameComponent name_comp {};
			name_comp.text = entity_doc.name.c_str();
			name_comp.guid = entity_guid;
			registry.template add<ecs::NameComponent>(entity, name_comp);
		}

		for (const auto& component : entity_doc.components) {

			switch (component.kind) {

				case scn::io::ComponentDoc::ComponentKind::Transform: {
					const scn::io::TransformDoc& transform_doc =
						std::get<scn::io::TransformDoc>(component.payload);

					ecs::TransformComponent transform_comp {};

					transform_comp.position = glm::make_vec3(transform_doc.position);
					transform_comp.rotation = quat(
						transform_doc.rotation[3],
						transform_doc.rotation[0],
						transform_doc.rotation[1],
						transform_doc.rotation[2]
					);
					transform_comp.scale = glm::make_vec3(transform_doc.scale);

					mat4 translation = glm::translate(mat4(1.0f), transform_comp.position);
					mat4 rotation    = glm::mat4_cast(transform_comp.rotation);
					mat4 scale       = glm::scale(mat4(1.0f), transform_comp.scale);

					transform_comp.mtx_W = translation * rotation * scale;

					registry.template add<ecs::TransformComponent>(entity, transform_comp);
				}
				break;

				case scn::io::ComponentDoc::ComponentKind::Camera: {
					const scn::io::CameraDoc& camera_doc =
						std::get<scn::io::CameraDoc>(component.payload);

					ecs::CameraComponent camera_comp {};

					camera_comp.fov_deg = camera_doc.fov_deg;
					camera_comp.aspect  = 1.0f;
					camera_comp.znear   = camera_doc.znear;
					camera_comp.zfar    = camera_doc.zfar;
					camera_comp.active  = camera_doc.active ? 1U : 0U;

					registry.template add<ecs::CameraComponent>(entity, camera_comp);
				}
				break;

				case scn::io::ComponentDoc::ComponentKind::Light: {
					const scn::io::LightDoc& light_doc =
						std::get<scn::io::LightDoc>(component.payload);

					ecs::LightComponent light_comp {};

					light_comp.type      = static_cast<scn::LightType>(static_cast<uint8_t>(light_doc.type));
					light_comp.enabled   = light_doc.enabled ? 1U : 0U;
					light_comp.intensity = light_doc.intensity;
					light_comp.range     = light_doc.range;
					light_comp.inner_deg = light_doc.inner_deg;
					light_comp.outer_deg = light_doc.outer_deg;
					light_comp.color_rgb = glm::make_vec3(light_doc.color_rgb);

					registry.template add<ecs::LightComponent>(entity, light_comp);
				}
				break;

				/* model forge */

				case scn::io::ComponentDoc::ComponentKind::Model:
				{
					const scn::io::ModelDoc& model_doc =
						std::get<scn::io::ModelDoc>(component.payload);

					Handle<res::ImportModel> hnd_model =
						asset_keeper.import_gltf_model(model_doc.gltf_path.c_str());

					const res::ImportModel* import_model =
						asset_keeper.storage<res::ImportModel>().find(hnd_model);

					if (!import_model || import_model->nodes.empty()) {
						HPR_ERROR(
							log::LogCategory::scene,
							"[instantiate] model import failed [%s]",
							model_doc.gltf_path.c_str()
						);
						break;
					}

					auto& cull_rig = scene.cull_rig;

					if (!model_cache.contains(hnd_model)) {
						model_cache[hnd_model] = render_forge.create_model(*import_model);

						rdr::Model& model = model_cache[hnd_model];

						/* pack occludee hulls */

						model.occludee_hull_base_idx =
							static_cast<uint32_t>(cull_rig.hull_geoslices.size());

						for (size_t i = 0; i < model.hull_geoslices.size(); ++i) {
							const auto& geoslice = model.hull_geoslices[i];
							const auto& submesh  = model.hull_subwires[i];

							const uint32_t vtx_base = static_cast<uint32_t>(cull_rig.hull_positions.size());
							const uint32_t idx_base = static_cast<uint32_t>(cull_rig.hull_indices.size());

							const vec3* hull_pos_src = model.hull_positions.data() + geoslice.vtx_base;

							cull_rig.hull_positions.insert(
								cull_rig.hull_positions.end(),
								hull_pos_src,
								hull_pos_src + geoslice.vtx_count
							);

							const uint32_t* hull_idx_src = model.hull_indices.data() + geoslice.idx_first;

							cull_rig.hull_indices.insert(
								cull_rig.hull_indices.end(),
								hull_idx_src,
								hull_idx_src + geoslice.idx_count
							);

							cull_rig.hull_geoslices.push_back({
								.vtx_base  = vtx_base,
								.vtx_count = geoslice.vtx_count,
								.idx_first = idx_base,
								.idx_count = geoslice.idx_count
							});

							cull_rig.hull_subwires.push_back(submesh); 
						}

						/* pack occluder twins */

						if (import_model->is_occluder) {
							const uint32_t vtx_base = static_cast<uint32_t>(cull_rig.twin_positions.size());
							const uint32_t idx_base = static_cast<uint32_t>(cull_rig.twin_indices.size());

							const vec3* twin_pos_src =
								model.twin_positions.data() + model.twin_geoslice.vtx_base;

							cull_rig.twin_positions.insert(
								cull_rig.twin_positions.end(),
								twin_pos_src,
								twin_pos_src + model.twin_geoslice.vtx_count
							);

							const uint32_t* twin_idx_src =
								model.twin_indices.data() + model.twin_geoslice.idx_first;

							cull_rig.twin_indices.insert(
								cull_rig.twin_indices.end(),
								twin_idx_src,
								twin_idx_src + model.twin_geoslice.idx_count
							);

							cull_rig.twin_geoslices.push_back({
								.vtx_base  = vtx_base,
								.vtx_count = model.twin_geoslice.vtx_count,
								.idx_first = idx_base,
								.idx_count = model.twin_geoslice.idx_count
							});

							cull_rig.twin_subwires.push_back(model.twin_subwire);
							
							model.occluder_twin_idx = static_cast<uint32_t>(cull_rig.twin_geoslices.size() - 1);
						}
					}

					const rdr::Model& model = model_cache[hnd_model];

					const uint32_t prims_before =
						static_cast<uint32_t>(scene.render_rig.primitives.size());

					render_forge.emit_primitives(
						entity,
						*import_model,
						model,
						scene.render_rig
					);

					const uint32_t prims_after =
						static_cast<uint32_t>(scene.render_rig.primitives.size());

					ecs::ModelComponent model_comp {
						.prim_first = prims_before,
						.prim_count = prims_after - prims_before
					};

					registry.template add<ecs::ModelComponent>(entity, model_comp);

					if (import_model->is_occluder) {

						ecs::OccluderComponent occluder_comp {
							.twin_idx = model.occluder_twin_idx,
							.mtx_L    = import_model->mtx_L_occluder
						};

						registry.template add<ecs::OccluderComponent>(entity, occluder_comp);
					}
				}
				break;
			}
		}
	}

	/* build hierarchy */

	for (const auto& entity_doc : scene_doc.entity_docs) {

		const uint64_t entity_guid = parse_guid_hex(entity_doc.guid);

		auto child_it = guid_entity_map.find(entity_guid);
		if (child_it == guid_entity_map.end())
			continue;

		ecs::Entity child_entity = child_it->second;

		ecs::HierarchyComponent hierarchy_comp {};

		hierarchy_comp.parent       = ecs::ctx::invalid_entity;
		hierarchy_comp.first_child  = ecs::ctx::invalid_entity;
		hierarchy_comp.next_sibling = ecs::ctx::invalid_entity;

		if (!entity_doc.parent_guid.empty()) {

			const uint64_t parent_guid = parse_guid_hex(entity_doc.parent_guid);

			auto parent_it = guid_entity_map.find(parent_guid);
			if (parent_it != guid_entity_map.end()) {
				hierarchy_comp.parent = parent_it->second;
			}
			else {
				HPR_WARN(
					log::LogCategory::scene,
					"[instantiate] parent not found [child %llu][parent %llu]",
					static_cast<unsigned long long>(entity_guid),
					static_cast<unsigned long long>(parent_guid)
				);
			}
		}

		registry.template add<ecs::HierarchyComponent>(child_entity, hierarchy_comp);
	}

	auto parent_children_map =
		mtp::make_unordered_map<
			ecs::Entity,
			mtp::vault<ecs::Entity, mtp::default_set>,
			mtp::default_set
		>();

	parent_children_map.reserve(scene_doc.entity_docs.size());

	for (const auto& entity_doc : scene_doc.entity_docs) {

		const uint64_t child_guid = parse_guid_hex(entity_doc.guid);
		const ecs::Entity child_entity = guid_entity_map[child_guid];

		const ecs::HierarchyComponent* child_hierarchy =
			registry.template get<ecs::HierarchyComponent>(child_entity);

		const ecs::Entity parent_entity =
			(child_hierarchy && child_hierarchy->parent != ecs::ctx::invalid_entity)
				? child_hierarchy->parent
				: ecs::ctx::invalid_entity;

		auto& children_list = parent_children_map[parent_entity];
		children_list.emplace_back(parent_entity);
		children_list.back() = child_entity;
	}

	for (auto& parent_children_pair : parent_children_map) {

		const ecs::Entity parent_entity = parent_children_pair.first;
		const auto& children            = parent_children_pair.second;

		const ecs::Entity first_child_entity =
			children.empty() ? ecs::ctx::invalid_entity : children[0];

		if (parent_entity != ecs::ctx::invalid_entity) {
			if (auto* parent_hierarchy =
					registry.template get<ecs::HierarchyComponent>(parent_entity)) {
				parent_hierarchy->first_child = first_child_entity;
			}
		}

		for (size_t child_idx = 0; child_idx < children.size(); ++child_idx) {

			const ecs::Entity child_entity = children[child_idx];
			if (child_entity == parent_entity)
				continue;

			if (auto* child_hierarchy =
					registry.template get<ecs::HierarchyComponent>(child_entity)) {
				child_hierarchy->next_sibling =
					(child_idx + 1 < children.size())
						? children[child_idx + 1]
						: ecs::ctx::invalid_entity;
			}
		}
	}

	return true;
}

} // hpr::scn

