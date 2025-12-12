#pragma once

#include <stdint.h>
#include <charconv>
#include <limits>

#include "mtp_memory.hpp"

#include "log.hpp"

#include "ecs_registry.hpp"
#include "asset_keeper.hpp"
#include "render_forge.hpp"
#include "scene.hpp"
#include "scene_io_data.hpp"
#include "components_scene.hpp"
#include "components_render.hpp"


namespace hpr::scn {


constexpr std::string_view k_hex_lower_prefix = "0x";
constexpr std::string_view k_hex_upper_prefix = "0X";

constexpr uint32_t k_hex_base     = 16;
constexpr uint64_t k_invalid_guid = 0;


inline uint64_t parse_guid_hex(std::string_view guid_hex)
{
	if (guid_hex.starts_with(k_hex_lower_prefix) || guid_hex.starts_with(k_hex_upper_prefix)) {
		guid_hex.remove_prefix(k_hex_lower_prefix.size());
	}

	uint64_t guid_value {};

	auto guid_result = std::from_chars(
		guid_hex.data(),
		guid_hex.data() + guid_hex.size(),
		guid_value,
		k_hex_base
	);

	return (guid_result.ec == std::errc {}) ? guid_value : k_invalid_guid;
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
	scene.clear();
	scene.set_ambient(scene_doc.ambient_rgb);


	/* mvp hardcoded tile layout */

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


	/* create entities and parent links */

	auto guid_entity_map =
		mtp::make_unordered_map<uint64_t, ecs::Entity, mtp::default_set>();

	guid_entity_map.reserve(scene_doc.entity_docs.size());

	auto guid_children_map =
		mtp::make_unordered_map<uint64_t, mtp::vault<uint64_t, mtp::default_set>, mtp::default_set>();

	guid_children_map.reserve(scene_doc.entity_docs.size());

	for (const auto& entity_doc : scene_doc.entity_docs) {

		if (entity_doc.guid.empty()) {
			HPR_WARN(
				log::LogCategory::scene,
				"[instantiate] missing entity guid"
			);
			continue;
		}

		const uint64_t entity_guid = parse_guid_hex(entity_doc.guid);
		if (entity_guid == k_invalid_guid) {
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

		scene.index(entity, entity_guid);
		guid_entity_map[entity_guid] = entity;

		if (!entity_doc.parent_guid.empty()) {
			const uint64_t parent_guid = parse_guid_hex(entity_doc.parent_guid);
			if (parent_guid == k_invalid_guid) {
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


	/* attach components and forge model render data */

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
					transform_comp.world = mat4(1.0f);

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

					if (!registry.template has<ecs::BoundComponent>(entity)) {

						ecs::BoundComponent bound_comp {};

						bound_comp.local_center = vec3(0.0f);
						bound_comp.local_half   = vec3(0.5f);
						bound_comp.world_center = vec3(0.0f);
						bound_comp.world_half   = vec3(0.5f);

						registry.template add<ecs::BoundComponent>(entity, bound_comp);
					}
				}
				break;

				case scn::io::ComponentDoc::ComponentKind::Model:
				{
					const scn::io::ModelDoc& model_doc =
						std::get<scn::io::ModelDoc>(component.payload);

					res::ImportModel import_model =
						asset_keeper.import_gltf_model(model_doc.mesh_path.c_str());

					if (import_model.primitives.empty()) {
						HPR_ERROR(
							log::LogCategory::scene,
							"[instantiate] model import failed [guid %llu][path %s]",
							static_cast<unsigned long long>(entity_guid),
							model_doc.mesh_path.c_str()
						);
						break;
					}

					auto& scene_primitives = scene.scene_primitives();
					scene_primitives.reserve(
						scene_primitives.size() + import_model.primitives.size()
					);

					ecs::ModelComponent model_comp {};
					ecs::BoundComponent bound_comp {};

					model_comp.submesh_first =
						static_cast<uint32_t>(scene_primitives.size());
					model_comp.submesh_count =
						static_cast<uint32_t>(import_model.primitives.size());

					vec3 aabb_min( std::numeric_limits<float>::max());
					vec3 aabb_max(-std::numeric_limits<float>::max());

					for (auto& import_primitive : import_model.primitives) {

						const uint8_t* vtx_bytes =
							import_primitive.geometry.vtx_bytes.data();
						const uint32_t vtx_count =
							import_primitive.geometry.vtx_count;

						for (uint32_t i = 0; i < vtx_count; ++i) {
							const auto* vertex =
								reinterpret_cast<const rdr::SceneVertex*>(
									vtx_bytes + i * sizeof(rdr::SceneVertex)
								);
							aabb_min = glm::min(aabb_min, vertex->pos);
							aabb_max = glm::max(aabb_max, vertex->pos);
						}

						scn::ScenePrimitive scene_primitive =
							render_forge.create_scene_primitive(import_primitive);

						scene_primitives.emplace_back(std::move(scene_primitive));
					}

					registry.template add<ecs::ModelComponent>(entity, model_comp);

					bound_comp.local_center = (aabb_min + aabb_max) * 0.5f;
					bound_comp.local_half   = (aabb_max - aabb_min) * 0.5f;
					bound_comp.world_center = bound_comp.local_center;
					bound_comp.world_half   = bound_comp.local_half;

					registry.template add<ecs::BoundComponent>(entity, bound_comp);
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

		hierarchy_comp.parent       = ecs::invalid_entity;
		hierarchy_comp.first_child  = ecs::invalid_entity;
		hierarchy_comp.next_sibling = ecs::invalid_entity;

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
			(child_hierarchy && child_hierarchy->parent != ecs::invalid_entity)
				? child_hierarchy->parent
				: ecs::invalid_entity;

		auto& children_list = parent_children_map[parent_entity];
		children_list.emplace_back(parent_entity);
		children_list.back() = child_entity;
	}

	for (auto& parent_children_pair : parent_children_map) {

		const ecs::Entity parent_entity = parent_children_pair.first;
		const auto& children            = parent_children_pair.second;

		const ecs::Entity first_child_entity =
			children.empty() ? ecs::invalid_entity : children[0];

		if (parent_entity != ecs::invalid_entity) {
			if (auto* parent_hierarchy =
					registry.template get<ecs::HierarchyComponent>(parent_entity)) {
				parent_hierarchy->first_child = first_child_entity;
			}
		}

		for (size_t i = 0; i < children.size(); ++i) {

			const ecs::Entity child_entity = children[i];
			if (child_entity == parent_entity)
				continue;

			if (auto* child_hierarchy =
					registry.template get<ecs::HierarchyComponent>(child_entity)) {
				child_hierarchy->next_sibling =
					(i + 1 < children.size())
						? children[i + 1]
						: ecs::invalid_entity;
			}
		}
	}

	return true;
}

} // hpr::scn

