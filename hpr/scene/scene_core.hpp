#pragma once

#include "hprint.hpp"

#include "mtp_memory.hpp"

#include "log.hpp"
#include "bvh_blas.hpp"
#include "scene_rig.hpp"
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


using ModelBlasVault = mtp::vault<mtp::vault<uint32_t, mtp::default_set>, mtp::default_set>;


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


static void ingest_cull_model(
	const res::ImportModel& import_model,
	rdr::Model&             model,
	scn::SceneCullRig&      cull_rig
)
{
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

	if (import_model.is_occluder) {
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


static ModelBlasVault ingest_spatial_model(
	const res::ImportModel& import_model,
	scn::SceneSpatialRig&   spatial_rig
)
{
	ModelBlasVault model_blases;
	model_blases.resize(import_model.meshes.size());

	const uint32_t spatial_vtx_base  = static_cast<uint32_t>(spatial_rig.positions.size());
	const uint32_t spatial_idx_first = static_cast<uint32_t>(spatial_rig.indices.size());

	uint32_t curr_vtx_base = spatial_vtx_base;
	uint32_t glob_tri_idx  = spatial_idx_first / 3;

	for (size_t mesh_idx = 0; mesh_idx < import_model.meshes.size(); ++mesh_idx) {
		const auto& import_mesh  = import_model.meshes[mesh_idx];
		const auto& pos_attr     = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::pos)];
		const vec3* pos_data     = reinterpret_cast<const vec3*>(pos_attr.blob.data());
		const uint32_t* idx_data = import_mesh.indices.data();
		const uint32_t vtx_count = static_cast<uint32_t>(pos_attr.blob.size() / sizeof(vec3));

		spatial_rig.positions.insert(spatial_rig.positions.end(), pos_data, pos_data + vtx_count);
		model_blases[mesh_idx].resize(import_mesh.submeshes.size());

		for (size_t sbm_idx = 0; sbm_idx < import_mesh.submeshes.size(); ++sbm_idx) {

			const auto& import_sbm   = import_mesh.submeshes[sbm_idx];
			const uint32_t tri_count = import_sbm.idx_count / 3;

			if (tri_count == 0) {
				model_blases[mesh_idx][sbm_idx] = 0xFFFFFFFFU;
				continue;
			}

			mtp::vault<vec3,     mtp::default_set> tri_mins(tri_count);
			mtp::vault<vec3,     mtp::default_set> tri_maxs(tri_count);
			mtp::vault<vec3,     mtp::default_set> tri_centroids(tri_count);
			mtp::vault<uint32_t, mtp::default_set> tri_idxs(tri_count);

			tri_mins.resize(tri_count);
			tri_maxs.resize(tri_count);
			tri_centroids.resize(tri_count);
			tri_idxs.resize(tri_count);

			uint32_t curr_glob_tri_first = glob_tri_idx;

			for (uint32_t idx_offs = 0; idx_offs < import_sbm.idx_count; idx_offs += 3) {

				uint32_t raw_idx_0 = import_mesh.indices[import_sbm.idx_first + idx_offs + 0];
				uint32_t raw_idx_1 = import_mesh.indices[import_sbm.idx_first + idx_offs + 1];
				uint32_t raw_idx_2 = import_mesh.indices[import_sbm.idx_first + idx_offs + 2];

				spatial_rig.indices.push_back(raw_idx_0 + curr_vtx_base);
				spatial_rig.indices.push_back(raw_idx_1 + curr_vtx_base);
				spatial_rig.indices.push_back(raw_idx_2 + curr_vtx_base);

				++glob_tri_idx;
			}

			uint32_t blas_node_first = static_cast<uint32_t>(spatial_rig.blas_nodes.size());

			geo::BlasBuilder blas_builder(curr_glob_tri_first, blas_node_first);

			mtp::vault<geo::BLBVH8Node, mtp::default_set> blas_sbm_nodes =
				blas_builder.build(
					pos_data,
					idx_data,
					import_sbm.vtx_base,
					import_sbm.idx_first,
					import_sbm.idx_count
				);

			spatial_rig.blas_nodes.insert(
				spatial_rig.blas_nodes.end(),
				blas_sbm_nodes.begin(),
				blas_sbm_nodes.end()
			);

			model_blases[mesh_idx][sbm_idx] = blas_node_first;
		}

		curr_vtx_base += vtx_count;
	}
	
	return model_blases;
}


static void ingest_anim_model(
	const res::ImportModel& import_model,
	rdr::Model&             model,
	scn::SceneAnimRig&      anim_rig
)
{
	if (import_model.skins.empty() || import_model.animations.empty()) {
		return;
	}

	model.skel_base_idx = static_cast<uint32_t>(anim_rig.skeletons.size());
	model.clip_base_idx = static_cast<uint32_t>(anim_rig.clips.size());
	
	const auto& import_skin = import_model.skins[0];
	model.bone_count = static_cast<uint32_t>(import_skin.joint_node_idxs.size());

	const uint32_t bind_mtx_inv_first =
		static_cast<uint32_t>(anim_rig.mtxs_inv_bind.size());

	anim_rig.mtxs_inv_bind.insert(
		anim_rig.mtxs_inv_bind.end(),
		import_skin.mtxs_inv_bind.begin(),
		import_skin.mtxs_inv_bind.end()
	);

	const uint32_t rest_mtx_L_first =
		static_cast<uint32_t>(anim_rig.mtxs_L_rest.size());

	anim_rig.mtxs_L_rest.insert(
		anim_rig.mtxs_L_rest.end(),
		import_skin.mtxs_L_rest.begin(),
		import_skin.mtxs_L_rest.end()
	);

	const uint32_t parent_idx_first = static_cast<uint32_t>(anim_rig.parent_idxs.size());
	anim_rig.parent_idxs.resize(parent_idx_first + model.bone_count, -1);

	for (uint32_t j = 0; j < model.bone_count; ++j) {
		uint32_t import_node_idx = import_skin.joint_node_idxs[j];
		uint32_t import_parent   = import_model.nodes[import_node_idx].parent;

		if (import_parent != std::numeric_limits<uint32_t>::max()) {
			auto p_it = std::find(
				import_skin.joint_node_idxs.begin(),
				import_skin.joint_node_idxs.end(),
				import_parent
			);
			if (p_it != import_skin.joint_node_idxs.end()) {
				anim_rig.parent_idxs[parent_idx_first + j] = static_cast<int16_t>(
					std::distance(import_skin.joint_node_idxs.begin(), p_it)
				);
			}
		}
	}

	anim_rig.skeletons.push_back({
		.bone_count         = model.bone_count,
		.parent_idx_first   = parent_idx_first,
		.bind_mtx_inv_first = bind_mtx_inv_first,
		.rest_mtx_L_first   = rest_mtx_L_first
	});

	for (const auto& import_clip : import_model.animations) {
		const uint32_t track_offset = static_cast<uint32_t>(anim_rig.tracks.size());
		const uint32_t track_count  = static_cast<uint32_t>(import_clip.tracks.size());

		for (const auto& import_track : import_clip.tracks) {
			const uint32_t key_count = static_cast<uint32_t>(import_track.times.size());

			auto it_bone = std::find(
				import_skin.joint_node_idxs.begin(),
				import_skin.joint_node_idxs.end(),
				import_track.node_idx
			);

			uint32_t bone_idx = (it_bone != import_skin.joint_node_idxs.end())
				? static_cast<uint32_t>(std::distance(import_skin.joint_node_idxs.begin(), it_bone))
				: 0xFFFFFFFFU;

			const uint32_t time_offset =
				static_cast<uint32_t>(anim_rig.key_times.size());
			const uint32_t translation_offset =
				static_cast<uint32_t>(anim_rig.key_tsls.size());
			const uint32_t rotation_offset =
				static_cast<uint32_t>(anim_rig.key_rots.size());

			anim_rig.key_times.insert(
				anim_rig.key_times.end(),
				import_track.times.begin(),
				import_track.times.end()
			);
			anim_rig.key_tsls.insert(
				anim_rig.key_tsls.end(),
				import_track.translations.begin(),
				import_track.translations.end()
			);
			anim_rig.key_rots.insert(
				anim_rig.key_rots.end(),
				import_track.rotations.begin(),
				import_track.rotations.end()
			);

			anim_rig.tracks.push_back({
				.bone_idx       = bone_idx,
				.key_count      = key_count,
				.key_time_first = time_offset,
				.key_tsl_first  = translation_offset,
				.key_rot_first  = rotation_offset
			});
		}

		anim_rig.clips.push_back({
			.duration_ticks = static_cast<uint32_t>(import_clip.duration_ticks * 1000.0f),
			.track_count    = track_count,
			.track_first    = track_offset
		});
	}
}


static void spawn_model_instance(
	ecs::Entity             entity,
	const res::ImportModel& import_model,
	const rdr::Model&       model,
	const ModelBlasVault&   asset_blas_roots,
	scn::SceneRenderRig&    render_rig,
	scn::SceneSpatialRig&   spatial_rig
)
{
	const uint32_t node_count = static_cast<uint32_t>(import_model.nodes.size());
	const bool is_skinned     = model.skel_base_idx != 0xFFFFFFFFU;

	mtp::vault<mat4, mtp::default_set> node_matrices;
	node_matrices.resize(node_count);

	for (uint32_t node_idx = 0; node_idx < node_count; ++node_idx) {
		const auto& node = import_model.nodes[node_idx];

		mat4 mtx_parent = (node.parent != 0xFFFFFFFFU) 
			? node_matrices[node.parent]
			: mat4(1.0f);

		node_matrices[node_idx] = mtx_parent * node.mtx_trs;

		if (node.mesh != 0xFFFFFFFFU) {

			const auto& import_mesh       = import_model.meshes[node.mesh];
			const uint32_t mesh_vtx_base  = model.mesh_vtx_bases[node.mesh];
			const uint32_t mesh_idx_first = model.mesh_idx_firsts[node.mesh];

			const vec3* pos_data = reinterpret_cast<const vec3*>(
				import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::pos)].blob.data()
			);

			for (uint32_t sbm_idx = 0; sbm_idx < import_mesh.submeshes.size(); ++sbm_idx) {
				const auto& import_submesh = import_mesh.submeshes[sbm_idx];

				vec3 aabb_min( std::numeric_limits<float>::max());
				vec3 aabb_max(-std::numeric_limits<float>::max());

				for (uint32_t i = 0; i < import_submesh.idx_count; ++i) {
					uint32_t raw_idx = import_mesh.indices[import_submesh.idx_first + i];

					vec3 baked_pos = vec3(
						node_matrices[node_idx] * vec4(pos_data[raw_idx + import_submesh.vtx_base], 1.0f)
					);

					aabb_min = glm::min(aabb_min, baked_pos);
					aabb_max = glm::max(aabb_max, baked_pos);
				}

				rdr::Submesh submesh {
					mesh_vtx_base  + import_submesh.vtx_base,
					mesh_idx_first + import_submesh.idx_first,
					import_submesh.idx_count
				};

				mat4 mtx_L  = node_matrices[node_idx];
				mat3 mtx_LN = glm::inverse(glm::transpose(mat3(mtx_L)));

				uint32_t material_idx = model.material_ssbo_idxs[import_submesh.model_mat_idx];

				uint32_t occludee_idx = model.occludee_hull_base_idx + import_submesh.hull_idx;
				uint32_t blas_root    = asset_blas_roots[node.mesh][sbm_idx];

				if (is_skinned) {
					render_rig.skin_submeshes.push_back(submesh);
					render_rig.skin_mtxs_L.push_back(mtx_L);
					render_rig.skin_mtxs_LN.push_back(mtx_LN);
					render_rig.skin_material_idxs.push_back(material_idx);
					render_rig.skin_entities.push_back(entity);

					render_rig.skin_mtxs_M.push_back(mat4(1.0f));
					render_rig.skin_aabb_L.push_back({aabb_min, aabb_max});
					render_rig.skin_occludee_idxs.push_back(occludee_idx);
				}
				else {
					render_rig.stat_submeshes.push_back(submesh);
					render_rig.stat_mtxs_L.push_back(mtx_L);
					render_rig.stat_mtxs_LN.push_back(mtx_LN);
					render_rig.stat_material_idxs.push_back(material_idx);
					render_rig.stat_entities.push_back(entity);

					render_rig.stat_mtxs_M.push_back(mat4(1.0f));
					render_rig.stat_aabb_L.push_back({aabb_min, aabb_max});
					render_rig.stat_occludee_idxs.push_back(occludee_idx);

					spatial_rig.blas_sbm_roots.push_back(blas_root);
				}
			}
		}
	}
}


template <typename... Components>
bool instantiate_scene(
	const scn::io::SceneDoc&      scene_doc,
	ecs::Registry<Components...>& registry,
	res::AssetKeeper&             asset_keeper,
	rdr::RenderForge&             render_forge,
	scn::Scene&                   scene
)
{
	scene = {};

	/* load scene environment */

	scene.render_rig.ambient_rgb = scene_doc.ambient_rgb;

	if (!scene_doc.environment.empty()) {
		Handle<res::ImageResource> env_img_hnd =
			asset_keeper.import_hdr_image(scene_doc.environment.c_str());

		if (env_img_hnd.is_valid()) {
			const res::ImageResource* env_img =
				asset_keeper.storage<res::ImageResource>().find(env_img_hnd);

			if (env_img) {
				render_forge.create_environment(
					env_img->pixels.data(),
					env_img->width,
					env_img->height
				);
			}
		}
	}

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

	/* parser caches */

	auto guid_entity_map =
		mtp::make_unordered_map<uint64_t, ecs::Entity, mtp::default_set>();
	guid_entity_map.reserve(scene_doc.entity_docs.size());

	auto guid_children_map =
		mtp::make_unordered_map<uint64_t, mtp::vault<uint64_t, mtp::default_set>, mtp::default_set>();
	guid_children_map.reserve(scene_doc.entity_docs.size());

	std::unordered_map<Handle<res::ImportModel>, rdr::Model, HandleHasher> model_cache;

	struct CachedBlas
	{
		Handle<res::ImportModel> model;
		ModelBlasVault           data;
	};

	mtp::vault<CachedBlas, mtp::default_set> asset_blas_cache;

	/* make entities and parent links */

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

					size_t cache_idx = 0;

					if (!model_cache.contains(hnd_model)) {

						rdr::Model& model = model_cache.emplace(
							hnd_model,
							render_forge.create_model(*import_model)
						).first->second;

						ingest_cull_model(*import_model, model, scene.cull_rig);
						
						auto new_mesh_blas =
							ingest_spatial_model(*import_model, scene.spatial_rig);

						asset_blas_cache.push_back({
							.model = hnd_model,
							.data  = std::move(new_mesh_blas)
						});
						cache_idx = asset_blas_cache.size() - 1;

						ingest_anim_model(*import_model, model, scene.anim_rig);
					}
					else {
						for (size_t i = 0; i < asset_blas_cache.size(); ++i) {
							if (asset_blas_cache[i].model == hnd_model) {
								cache_idx = i;
								break;
							}
						}
					}

					/* populate entity model instance */

					auto& cached_mesh_blas = asset_blas_cache[cache_idx].data;
					const rdr::Model& model = model_cache[hnd_model];
					const bool is_skinned   = model.skel_base_idx != 0xFFFFFFFFU;

					const uint32_t sbm_before = is_skinned
						? static_cast<uint32_t>(scene.render_rig.skin_submeshes.size())
						: static_cast<uint32_t>(scene.render_rig.stat_submeshes.size());

					spawn_model_instance(
						entity,
						*import_model,
						model,
						cached_mesh_blas,
						scene.render_rig,
						scene.spatial_rig
					);

					const uint32_t sbm_after = is_skinned
						? static_cast<uint32_t>(scene.render_rig.skin_submeshes.size())
						: static_cast<uint32_t>(scene.render_rig.stat_submeshes.size());

					ecs::ModelComponent model_comp {
						.sbm_first = sbm_before,
						.sbm_count = sbm_after - sbm_before
					};
					registry.template add<ecs::ModelComponent>(entity, model_comp);

					if (is_skinned) {
						const uint32_t instance_bone_base =
							static_cast<uint32_t>(scene.anim_rig.mtxs_M_bones.size());
						scene.anim_rig.mtxs_M_bones.resize(instance_bone_base + model.bone_count, mat4(1.0f));

						ecs::AnimComponent anim_comp {
							.skeleton_idx  = model.skel_base_idx,
							.clip_idx      = model.clip_base_idx,
							.local_time    = 0.0f,
							.base_pose_idx = instance_bone_base
						};
						
						registry.template add<ecs::AnimComponent>(entity, anim_comp);
					}

					if (import_model->is_occluder) {
						ecs::OccluderComponent occluder_comp {
							.twin_idx = model.occluder_twin_idx,
							.mtx_L    = import_model->mtx_L_occluder
						};
						registry.template add<ecs::OccluderComponent>(entity, occluder_comp);
					}
					break;
				}
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

