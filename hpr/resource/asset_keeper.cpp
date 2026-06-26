#include "hprint.hpp"

#include "asset_keeper.hpp"

#include "log.hpp"
#include "panic.hpp"
#include "mtp_memory.hpp"

#include "quickhull.hpp"
#include "asset_data.hpp"
#include "render_data.hpp"
#include "resource_ingest.hpp"

#include "stb_image.h"

#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstring>
#include <string_view>


namespace hpr::res {


AssetKeeper::~AssetKeeper()
{}


Handle<ImportModel> AssetKeeper::import_gltf_model(const char* path)
{
	std::string path_str = path;

	if (m_model_cache.contains(path_str)) {
		return m_model_cache[path_str];
	}

	Handle<GltfResource> hnd_gltf = load_gltf(path);

	const GltfResource* gltf_resource = m_gltf_bank.find(hnd_gltf);
	if (!gltf_resource || !gltf_resource->data) {
		HPR_ERROR(
			log::LogCategory::asset,
			"[import_gltf_model] failed to load gltf [%s]",
			path
		);
		return {};
	}

	const cgltf_data* gltf_data = gltf_resource->data;

	ImportModel import_model {};

	/* materials */

	import_model.materials.resize(gltf_data->materials_count);

	for (cgltf_size i = 0; i < gltf_data->materials_count; ++i) {
		import_model.materials[i] = import_gltf_material(
			path,
			gltf_data,
			&gltf_data->materials[i]
		);
	}

	/* locate monolithic occluder */

	uint32_t occ_mesh_idx = std::numeric_limits<uint32_t>::max();
	mat4 occluder_mtx_local {1.0f};

	for (cgltf_size node_idx = 0; node_idx < gltf_data->nodes_count; ++node_idx) {
		const cgltf_node& node = gltf_data->nodes[node_idx];

		if (node.name && strstr(node.name, "OCCLUDER") != nullptr) {
			if (node.mesh) {
				occ_mesh_idx = static_cast<uint32_t>(cgltf_mesh_index(gltf_data, node.mesh));
				cgltf_node_transform_local(&node, &occluder_mtx_local[0][0]);
			}
			break;
		}
	}

	/* extract occluder geometry */

	if (occ_mesh_idx != std::numeric_limits<uint32_t>::max()) {
		const cgltf_mesh& gltf_mesh = gltf_data->meshes[occ_mesh_idx];

		uint32_t twin_vtx_cursor = 0;
		uint32_t twin_idx_cursor = 0;

		for (cgltf_size prim_idx = 0; prim_idx < gltf_mesh.primitives_count; ++prim_idx) {
			
			const cgltf_primitive& gltf_primitive = gltf_mesh.primitives[prim_idx];
			VtxAccessors vtx_accessors = extract_vtx_accessors(gltf_primitive);

			if (!vtx_accessors.pos) {
				continue;
			}

			const uint32_t prim_vtx_count = static_cast<uint32_t>(vtx_accessors.pos->count);
			import_model.twin_positions.resize(twin_vtx_cursor + prim_vtx_count);

			cgltf_accessor_unpack_floats(
				vtx_accessors.pos,
				reinterpret_cast<float*>(import_model.twin_positions.data() + twin_vtx_cursor),
				prim_vtx_count * vec3::length()
			);

			uint32_t expected_idx_count = gltf_primitive.indices
				? static_cast<uint32_t>(gltf_primitive.indices->count)
				: prim_vtx_count;

			import_model.twin_indices.resize(twin_idx_cursor + expected_idx_count);

			uint32_t prim_idx_count = extract_mesh_indices(
				gltf_primitive,
				import_model.twin_indices,
				twin_idx_cursor,
				twin_vtx_cursor
			);

			twin_vtx_cursor += prim_vtx_count;
			twin_idx_cursor += prim_idx_count;
		}

		import_model.occluder_twin = geo::Geoslice {
			.vtx_base  = 0,
			.vtx_count = twin_vtx_cursor,
			.idx_first = 0,
			.idx_count = twin_idx_cursor
		};

		import_model.mtx_L_occluder = occluder_mtx_local;
		import_model.is_occluder    = true;
	}
	else {
		import_model.is_occluder = false;
	}

	/* visual model mesh remap */

	mtp::vault<uint32_t, mtp::default_set> mesh_remap(
		gltf_data->meshes_count,
		std::numeric_limits<uint32_t>::max()
	);

	uint32_t vis_mesh_cnt = 0;

	for (cgltf_size mesh_idx = 0; mesh_idx < gltf_data->meshes_count; ++mesh_idx) {
		if (mesh_idx == occ_mesh_idx) {
			continue;
		}
		mesh_remap[mesh_idx] = vis_mesh_cnt++;
	}

	/* visual model mesh allocation */

	import_model.meshes.resize(vis_mesh_cnt);

	for (cgltf_size mesh_idx = 0; mesh_idx < gltf_data->meshes_count; ++mesh_idx) {

		if (mesh_remap[mesh_idx] == std::numeric_limits<uint32_t>::max()) {
			continue;
		}

		const cgltf_mesh& gltf_mesh = gltf_data->meshes[mesh_idx];
		ImportMesh& import_mesh     = import_model.meshes[mesh_remap[mesh_idx]];

		allocate_mesh_memory(gltf_mesh, import_mesh);

		uint32_t vtx_cursor = 0;
		uint32_t idx_cursor = 0;

		for (cgltf_size prim_idx = 0; prim_idx < gltf_mesh.primitives_count; ++prim_idx) {

			const cgltf_primitive& gltf_primitive = gltf_mesh.primitives[prim_idx];

			VtxAccessors vtx_accessors =
				extract_vtx_accessors(gltf_primitive);

			const uint32_t prim_vtx_count =
				static_cast<uint32_t>(vtx_accessors.pos->count);

			auto& pos_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::pos)];
			if (pos_attr.blob.size() > 0) {
				if (vtx_accessors.pos) {

					size_t write_offset = vtx_cursor     * sizeof(vec3);
					size_t write_size   = prim_vtx_count * sizeof(vec3);
					HPR_ASSERT_MSG(
						write_offset + write_size <= pos_attr.blob.size(),
						"vertex pos overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.pos,
						reinterpret_cast<float*>(pos_attr.blob.data() + (vtx_cursor * sizeof(vec3))),
						prim_vtx_count * vec3::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing pos accessor",
						mesh_idx
					);
					return {};
				}
			}
			else {
				HPR_ERROR(
					log::LogCategory::asset,
					"[import_gltf_model] mesh [%zu] missing pos blob",
					mesh_idx
				);
				return {};
			}

			auto& nrm_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::nrm)];
			if (nrm_attr.blob.size() > 0) {
				if (vtx_accessors.nrm) {

					size_t write_offset = vtx_cursor     * sizeof(vec3);
					size_t write_size   = prim_vtx_count * sizeof(vec3);
					HPR_ASSERT_MSG(
						write_offset + write_size <= nrm_attr.blob.size(),
						"vertex nrm overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.nrm,
						reinterpret_cast<float*>(nrm_attr.blob.data() + (vtx_cursor * sizeof(vec3))),
						prim_vtx_count * vec3::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing nrm accessor",
						mesh_idx
					);
					return {};
				}
			}
			else {
				HPR_ERROR(
					log::LogCategory::asset,
					"[import_gltf_model] mesh [%zu] missing nrm blob",
					mesh_idx
				);
				return {};
			}

			auto& tan_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::tan)];
			if (tan_attr.blob.size() > 0) {
				if (vtx_accessors.tan) {

					size_t write_offset = vtx_cursor     * sizeof(vec4);
					size_t write_size   = prim_vtx_count * sizeof(vec4);
					HPR_ASSERT_MSG(
						write_offset + write_size <= tan_attr.blob.size(),
						"vertex tan overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.tan,
						reinterpret_cast<float*>(tan_attr.blob.data() + (vtx_cursor * sizeof(vec4))),
						prim_vtx_count * vec4::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing tan accessor",
						mesh_idx
					);
					return {};
				}
			}

			auto& uv0_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::uv0)];
			if (uv0_attr.blob.size() > 0) {
				if (vtx_accessors.uv0) {

					size_t write_offset = vtx_cursor     * sizeof(vec2);
					size_t write_size   = prim_vtx_count * sizeof(vec2);
					HPR_ASSERT_MSG(
						write_offset + write_size <= uv0_attr.blob.size(),
						"vertex uv0 overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.uv0,
						reinterpret_cast<float*>(uv0_attr.blob.data() + (vtx_cursor * sizeof(vec2))),
						prim_vtx_count * vec2::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing uv0 accessor",
						mesh_idx
					);
					return {};
				}
			}

			auto& uv1_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::uv1)];
			if (uv1_attr.blob.size() > 0) {
				if (vtx_accessors.uv1) {

					size_t write_offset = vtx_cursor     * sizeof(vec2);
					size_t write_size   = prim_vtx_count * sizeof(vec2);
					HPR_ASSERT_MSG(
						write_offset + write_size <= uv1_attr.blob.size(),
						"vertex uv1 overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.uv1,
						reinterpret_cast<float*>(uv1_attr.blob.data() + (vtx_cursor * sizeof(vec2))),
						prim_vtx_count * vec2::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing uv1 accessor",
						mesh_idx
					);
					return {};
				}
			}

			auto& rgb_attr = import_mesh.vtx_attributes[static_cast<size_t>(AttrType::rgb)];
			if (rgb_attr.blob.size() > 0) {
				if (vtx_accessors.rgb) {

					size_t write_offset = vtx_cursor     * sizeof(vec4);
					size_t write_size   = prim_vtx_count * sizeof(vec4);
					HPR_ASSERT_MSG(
						write_offset + write_size <= rgb_attr.blob.size(),
						"vertex rgb overflow"
					);

					cgltf_accessor_unpack_floats(
						vtx_accessors.rgb,
						reinterpret_cast<float*>(rgb_attr.blob.data() + (vtx_cursor * sizeof(vec4))),
						prim_vtx_count * vec4::length()
					);
				}
				else {
					HPR_ERROR(
						log::LogCategory::asset,
						"[import_gltf_model] mesh [%zu] missing rgb accessor",
						mesh_idx
					);
					return {};
				}
			}

			uint32_t prim_idx_count =
				extract_mesh_indices(gltf_primitive, import_mesh.indices, idx_cursor, vtx_cursor);

			uint32_t mat_idx = 0;
			if (gltf_primitive.material) {
				mat_idx = static_cast<uint32_t>(cgltf_material_index(gltf_data, gltf_primitive.material));
			}
			else {
				HPR_ERROR(
					log::LogCategory::asset,
					"[import_gltf_model] mesh [%zu] prim [%zu] missing material",
					mesh_idx,
					prim_idx
				);
				return {};
			}

			uint32_t curr_hull_idx = 0xFFFFFFFF;

			const vec3* raw_positions = reinterpret_cast<const vec3*>(
				pos_attr.blob.data() + (vtx_cursor * sizeof(vec3))
			);

			geo::HullRaw hull = geo::compute_convex_hull(raw_positions, prim_vtx_count);

			if (hull.is_valid) {
				curr_hull_idx = static_cast<uint32_t>(import_model.hulls.size());

				uint32_t hull_vtx_base = static_cast<uint32_t>(import_model.hull_positions.size());
				uint32_t hull_idx_base = static_cast<uint32_t>(import_model.hull_indices.size());

				import_model.hull_positions.insert(
					import_model.hull_positions.end(),
					hull.vertices.begin(),
					hull.vertices.end()
				);

				import_model.hull_indices.insert(
					import_model.hull_indices.end(),
					hull.indices.begin(),
					hull.indices.end()
				);

				import_model.hulls.emplace_back(ImportHull {
					.vtx_base  = hull_vtx_base,
					.vtx_count = static_cast<uint32_t>(hull.vertices.size()),
					.idx_base  = hull_idx_base,
					.idx_count = static_cast<uint32_t>(hull.indices.size())
				});
			}

			ImportSubmesh submesh {
				.vtx_base      = vtx_cursor,
				.idx_first     = idx_cursor,
				.idx_count     = prim_idx_count,
				.model_mat_idx = mat_idx,
				.hull_idx      = curr_hull_idx
			};

			import_mesh.submeshes.emplace_back(submesh);

			vtx_cursor += prim_vtx_count;
			idx_cursor += prim_idx_count;
		}
	}


	/* node hierarchy culling */

	mtp::vault<bool,     mtp::default_set> drop_node(gltf_data->nodes_count, false);
	mtp::vault<uint32_t, mtp::default_set> drop_stack;

	/* roots of the occluder trees */

	for (cgltf_size node_idx = 0; node_idx < gltf_data->nodes_count; ++node_idx) {

		const cgltf_node& node = gltf_data->nodes[node_idx];

		if (node.name && strstr(node.name, "OCCLUDER") != nullptr) {
			drop_node[node_idx] = true;
			drop_stack.push_back(static_cast<uint32_t>(node_idx));
		}
		else if (node.mesh) {

			uint32_t mesh_idx = static_cast<uint32_t>(cgltf_mesh_index(gltf_data, node.mesh));

			if (mesh_remap[mesh_idx] == std::numeric_limits<uint32_t>::max()) {
				drop_node[node_idx] = true;
				drop_stack.push_back(static_cast<uint32_t>(node_idx));
			}
		}
	}

	/* occluder hierarchy stack propagation */

	while (!drop_stack.empty()) {
			uint32_t current_idx = drop_stack.back();
			drop_stack.pop_back();

			const cgltf_node& node = gltf_data->nodes[current_idx];

			for (cgltf_size ch = 0; ch < node.children_count; ++ch) {
				uint32_t child_idx =
					static_cast<uint32_t>(cgltf_node_index(gltf_data, node.children[ch]));
				
				if (!drop_node[child_idx]) {
					drop_node[child_idx] = true;
					drop_stack.push_back(child_idx);
				}
			}
		}

	/* model node remap */

	mtp::vault<uint32_t, mtp::default_set> node_remap(
		gltf_data->nodes_count,
		std::numeric_limits<uint32_t>::max()
	);

	mtp::vault<uint32_t, mtp::default_set> model_nodes;

	for (cgltf_size node_idx = 0; node_idx < gltf_data->nodes_count; ++node_idx) {
		if (!drop_node[node_idx]) {
			node_remap[node_idx] = static_cast<uint32_t>(model_nodes.size());
			model_nodes.push_back(static_cast<uint32_t>(node_idx));
		}
	}

	import_model.nodes.resize(model_nodes.size());

	uint32_t child_cursor = 0;

	for (uint32_t new_idx = 0; new_idx < model_nodes.size(); ++new_idx) {

		uint32_t old_idx            = model_nodes[new_idx];
		const cgltf_node& gltf_node = gltf_data->nodes[old_idx];
		ImportNode& node            = import_model.nodes[new_idx];

		mat4 mtx_node_trs {1.0f};
		cgltf_node_transform_local(&gltf_node, &mtx_node_trs[0][0]);
		node.mtx_trs = mtx_node_trs;

		node.mesh = gltf_node.mesh
			? mesh_remap[cgltf_mesh_index(gltf_data, gltf_node.mesh)]
			: std::numeric_limits<uint32_t>::max();

		node.parent = gltf_node.parent
			? node_remap[cgltf_node_index(gltf_data, gltf_node.parent)]
			: std::numeric_limits<uint32_t>::max();

		node.child_first = child_cursor;

		uint32_t model_child_cnt = 0;

		for (cgltf_size ch = 0; ch < gltf_node.children_count; ++ch) {
			uint32_t old_child_idx =
				static_cast<uint32_t>(cgltf_node_index(gltf_data, gltf_node.children[ch]));

			if (node_remap[old_child_idx] != std::numeric_limits<uint32_t>::max()) {
				model_child_cnt++;
			}
		}

		node.child_count = model_child_cnt;
		child_cursor    += model_child_cnt;
	}

	import_model.node_child_idxs.resize(child_cursor);

	uint32_t child_idx_cursor = 0;

	for (uint32_t new_idx = 0; new_idx < model_nodes.size(); ++new_idx) {

		uint32_t old_idx = model_nodes[new_idx];
		const cgltf_node& gltf_node = gltf_data->nodes[old_idx];

		for (cgltf_size ch = 0; ch < gltf_node.children_count; ++ch) {

			const cgltf_node* child = gltf_node.children[ch];

			uint32_t old_child_idx =
				static_cast<uint32_t>(cgltf_node_index(gltf_data, child));

			HPR_ASSERT_MSG(old_child_idx < gltf_data->nodes_count,
				"child index oob");
			HPR_ASSERT_MSG(child->parent == &gltf_node,
				"parent / child mismatch");

			uint32_t new_child_idx = node_remap[old_child_idx];

			if (new_child_idx != std::numeric_limits<uint32_t>::max()) {
				import_model.node_child_idxs[child_idx_cursor++] = new_child_idx;
			}
		}
	}

	Handle<ImportModel> hnd_model = m_model_bank.add(path_str, std::move(import_model)).handle;

	m_model_cache[path_str] = hnd_model;

	return hnd_model;
}


Handle<GltfResource> AssetKeeper::load_gltf(const char* path)
{
	if (auto* existing_asset = m_gltf_bank.find(path))
		return existing_asset->handle;

	cgltf_options options {};
	cgltf_data* data = nullptr;

	if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
		return Handle<GltfResource>::null();
	}

	if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
		cgltf_free(data);
		return Handle<GltfResource>::null();
	}

	cgltf_result validate_result = cgltf_validate(data);

	if (validate_result != cgltf_result_success) {
		HPR_ERROR(
			log::LogCategory::asset,
			"[load_gltf] cgltf_validate failed [%d][%s]",
			static_cast<int>(validate_result),
			path
		);
	}

	Asset<GltfResource>& asset = m_gltf_bank.add(path, data);
	return asset.handle;
}


void AssetKeeper::allocate_mesh_memory(const cgltf_mesh& gltf_mesh, ImportMesh& import_mesh)
{
	uint32_t vtx_total = 0;
	uint32_t idx_total = 0;
	bool active_attributes[static_cast<size_t>(AttrType::count)] = {false};

	for (cgltf_size prim_idx = 0; prim_idx < gltf_mesh.primitives_count; ++prim_idx) {
		const cgltf_primitive& prim = gltf_mesh.primitives[prim_idx];

		for (cgltf_size attr_idx = 0; attr_idx < prim.attributes_count; ++attr_idx) {
			if (prim.attributes[attr_idx].type == cgltf_attribute_type_position) {
				vtx_total += static_cast<uint32_t>(prim.attributes[attr_idx].data->count);
				break;
			}
		}

		if (prim.indices) {
			idx_total += static_cast<uint32_t>(prim.indices->count);
		}

		for (cgltf_size attr_idx = 0; attr_idx < prim.attributes_count; ++attr_idx) {
			AttrType type = get_standard_attr_type(prim.attributes[attr_idx]);
			if (type != AttrType::count) {
				active_attributes[static_cast<size_t>(type)] = true;
			}
		}
	}

	for (size_t attr_idx = 0; attr_idx < static_cast<size_t>(AttrType::count); ++attr_idx) {

		if (!active_attributes[attr_idx])
			continue;

		VtxAttribute& raw_attr = import_mesh.vtx_attributes[attr_idx];
		
		raw_attr.format          = get_standard_format(static_cast<AttrType>(attr_idx));
		raw_attr.component_count = get_standard_components(static_cast<AttrType>(attr_idx));

		uint32_t stride =
			get_format_size(raw_attr.format) * static_cast<uint32_t>(raw_attr.component_count);
		
		raw_attr.blob.resize(vtx_total * stride);
	}

	import_mesh.indices.resize(idx_total);
	import_mesh.submeshes.reserve(gltf_mesh.primitives_count);
}


AssetKeeper::VtxAccessors AssetKeeper::extract_vtx_accessors(const cgltf_primitive& primitive)
{
	VtxAccessors accessors {};

	for (cgltf_size attr_idx = 0; attr_idx < primitive.attributes_count; ++attr_idx) {
		const cgltf_attribute& attr = primitive.attributes[attr_idx];

		switch (attr.type) {
			case cgltf_attribute_type_position:
				accessors.pos = attr.data;
			break;
			case cgltf_attribute_type_normal:
				accessors.nrm = attr.data;
			break;
			case cgltf_attribute_type_tangent:
				accessors.tan = attr.data;
			break;
			case cgltf_attribute_type_texcoord:
				if (attr.index == 0)
					accessors.uv0 = attr.data;
				else if (attr.index == 1)
					accessors.uv1 = attr.data;
			break;
			case cgltf_attribute_type_color:
				if (attr.index == 0)
					accessors.rgb = attr.data;
			break;
			default: break;
		}
	}
	return accessors;
}


void AssetKeeper::extract_vtx_attribute(
	const cgltf_accessor*                  accessor,
	mtp::vault<uint8_t, mtp::default_set>& mesh_attrib_vlt,
	uint32_t                               start_idx,
	uint32_t                               count
)
{
	if (!accessor || !accessor->buffer_view) {
		return;
	}

	uint8_t* src_ptr =
		static_cast<uint8_t*>(accessor->buffer_view->buffer->data) +
		accessor->buffer_view->offset                              +
		accessor->offset;

	size_t write_start = start_idx * accessor->stride;
	size_t write_size  = count     * accessor->stride;
	HPR_ASSERT_MSG(
		write_start + write_size <= mesh_attrib_vlt.size(),
		"attribute extraction overflow"
	);

	std::memcpy(
		&mesh_attrib_vlt[start_idx * accessor->stride],
		src_ptr,
		count * accessor->stride
	);
}


uint32_t AssetKeeper::extract_mesh_indices(
	const cgltf_primitive&                  primitive,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	uint32_t                                idx_cursor,
	uint32_t                                vtx_cursor
)
{
	if (!primitive.indices) {
		return 0;
	}

	const uint32_t src_count = static_cast<uint32_t>(primitive.indices->count);
	uint32_t written_count = 0;

	switch (primitive.type) {
		case cgltf_primitive_type_triangles: {
			for (uint32_t i = 0; i < src_count; ++i) {

				HPR_ASSERT_MSG(
					idx_cursor + i < indices.size(),
					"index buffer overflow"
				);

				indices[idx_cursor + i] =
					static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
			}
			written_count = src_count;
			break;
		}

		case cgltf_primitive_type_triangle_strip: {
			const uint32_t trig_count = src_count - 2;
			for (uint32_t i = 0; i < trig_count; ++i) {
				uint32_t i_0 = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
				uint32_t i_1 = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 1));
				uint32_t i_2 = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 2));

				uint32_t out_base = idx_cursor + (i * 3);
				if (i % 2 == 0) {
					indices[out_base + 0] = i_0;
					indices[out_base + 1] = i_1;
					indices[out_base + 2] = i_2;
				} else {
					indices[out_base + 0] = i_0;
					indices[out_base + 1] = i_2;
					indices[out_base + 2] = i_1;
				}
			}
			written_count = trig_count * 3;
			break;
		}

		case cgltf_primitive_type_triangle_fan: {
			const uint32_t trig_count = src_count - 2;
			uint32_t i_0 = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, 0));
			
			for (uint32_t i = 0; i < trig_count; ++i) {
				uint32_t out_base = idx_cursor + (i * 3);
				indices[out_base + 0] = i_0;
				indices[out_base + 1] = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 1));
				indices[out_base + 2] = vtx_cursor + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 2));
			}
			written_count = trig_count * 3;
			break;
		}

		default: {
			HPR_ERROR(
				log::LogCategory::asset,
				"[extract_mesh_indices] unsupported primitive [%d]",
				primitive.type);
			break;
		}
	}

	return written_count;
}


Handle<MaterialResource> AssetKeeper::import_gltf_material(
	const char*           gltf_path,
	const cgltf_data*     gltf_root,
	const cgltf_material* gltf_material
)
{
	if (!gltf_material) {
		return Handle<MaterialResource>::null();
	}

	const uint32_t material_index = 
		static_cast<uint32_t>(cgltf_material_index(gltf_root, gltf_material));

	if (auto* existing_material =
			m_material_template_bank.find_composite(gltf_path, material_index)) {
		return existing_material->handle;
	}

	MaterialResource material_res =
		make_gltf_material(*gltf_material, gltf_path, gltf_root);

	Asset<MaterialResource>& asset =
		m_material_template_bank.add_composite(
			gltf_path,
			material_index,
			material_res
		);

	return asset.handle;
}


MaterialResource AssetKeeper::make_gltf_material(
	const cgltf_material& material,
	const char*           gltf_path,
	const cgltf_data*     gltf_root
)
{
	MaterialResource material_res {};

	material_res.map_mask = 0;
	for (uint32_t i = 0; i < max_tex_per_mat; ++i)
		material_res.uv_index[i] = 0;

	auto assign_tex =
		[this, &material_res, &gltf_path, &gltf_root]
		(const cgltf_texture* tex, int slot)
		{
			auto key_opt = make_gltf_image_key(tex, gltf_path, gltf_root);
			if (key_opt) {
				const char* key = key_opt->data();
				if (auto* existing_tex = m_image_bank.find(key)) {
					material_res.textures[slot] = existing_tex->handle;
					material_res.map_mask |= (1U << slot);
				}
				else {
					Handle<ImageResource> img_hnd =
						import_gltf_image(tex, gltf_path, gltf_root);
					if (img_hnd.is_valid()) {
						material_res.textures[slot] = img_hnd;
						material_res.map_mask |= (1U << slot);
					}
				}
			}
			else {
				Handle<ImageResource> img_hnd =
					import_gltf_image(tex, gltf_path, gltf_root);
				if (img_hnd.is_valid()) {
					material_res.textures[slot] = img_hnd;
					material_res.map_mask |= (1U << slot);
				}
			}
		};

	cgltf_texture* mr_source_tex =
		material.has_pbr_metallic_roughness ?
		material.pbr_metallic_roughness.metallic_roughness_texture.texture :
		nullptr;

	cgltf_texture* ao_source_tex = material.occlusion_texture.texture;

	if (mr_source_tex || ao_source_tex) {
		char ormh_key[cfg::max_path_length];

		std::snprintf(
			ormh_key,
			sizeof(ormh_key),
			"ormh_%p_%p",
			static_cast<void*>(mr_source_tex),
			static_cast<void*>(ao_source_tex)
		);

		if (auto* existing_tex = m_image_bank.find(ormh_key)) {
			material_res.textures[tex_ormh] = existing_tex->handle;
			material_res.map_mask |= (1U << tex_ormh);
		}
		else {
			Handle<ImageResource> hnd_mr;
			Handle<ImageResource> hnd_ao;

			const ImageResource* img_res_mr = nullptr;
			const ImageResource* img_res_ao = nullptr;

			if (mr_source_tex) {
				hnd_mr = import_gltf_image(mr_source_tex, gltf_path, gltf_root);
				img_res_mr = m_image_bank.find(hnd_mr);
			}

			if (ao_source_tex) {
				hnd_ao = import_gltf_image(ao_source_tex, gltf_path, gltf_root);
				img_res_ao = m_image_bank.find(hnd_ao);
			}

			uint32_t width_mr  = img_res_mr ? img_res_mr->width  : 0;
			uint32_t height_mr = img_res_mr ? img_res_mr->height : 0;
			uint32_t width_ao  = img_res_ao ? img_res_ao->width  : 0;
			uint32_t height_ao = img_res_ao ? img_res_ao->height : 0;

			if (width_mr > 0 && width_ao > 0) {
				if (width_mr != width_ao || height_mr != height_ao) {
					HPR_ERROR(
						log::LogCategory::asset,
						"[make_gltf_material] ormh size mismatch [mr %ux%u][ao %ux%u][%s]",
						width_mr,
						height_mr,
						width_ao,
						height_ao,
						gltf_path
					);
					return material_res;
				}
			}

			ImageResource img_res_ormh {};
			img_res_ormh.width    = width_mr  > 0 ? width_mr  : width_ao;
			img_res_ormh.height   = height_mr > 0 ? height_mr : height_ao;
			img_res_ormh.channels = 4;

			const size_t pixel_count    = static_cast<size_t>(img_res_ormh.width) * img_res_ormh.height;
			const size_t expected_bytes = pixel_count * img_res_ormh.channels;
			
			if (img_res_ao) {
				HPR_ASSERT_MSG(
					img_res_ao->pixels.size() >= expected_bytes,
					"ao tex size mismatch"
				);
			}
			
			if (img_res_mr) {
				HPR_ASSERT_MSG(
					img_res_mr->pixels.size() >= expected_bytes,
					"mr tex size mismatch"
				);
			}

			img_res_ormh.pixels.resize(pixel_count * 4);

			uint8_t* img_ormh_pixels = img_res_ormh.pixels.data();

			for (size_t i = 0; i < pixel_count; ++i) {

				const uint8_t* ao = (img_res_ao && img_res_ao->pixels.size() >= pixel_count * 4)
					? img_res_ao->pixels.data()
					: nullptr;

				const uint8_t* mr = (img_res_mr && img_res_mr->pixels.size() >= pixel_count * 4)
					? img_res_mr->pixels.data()
					: nullptr;

				uint8_t occl  = (img_res_ao && img_res_ao->pixels.size())
					? img_res_ao->pixels[i * 4 + static_cast<uint8_t>(rdr::MaterialMap::Slot::alb)]
					: 255;
				
				uint8_t rough = (img_res_mr && img_res_mr->pixels.size())
					? img_res_mr->pixels[i * 4 + static_cast<uint8_t>(rdr::MaterialMap::Slot::nrm)]
					: 255;
				
				uint8_t metal = (img_res_mr && img_res_mr->pixels.size())
					? img_res_mr->pixels[i * 4 + static_cast<uint8_t>(rdr::MaterialMap::Slot::orm)]
					: 255;

				img_ormh_pixels[i * 4 + static_cast<uint8_t>(rdr::ORMH::Slot::occ)] = occl;
				img_ormh_pixels[i * 4 + static_cast<uint8_t>(rdr::ORMH::Slot::rgh)] = rough;
				img_ormh_pixels[i * 4 + static_cast<uint8_t>(rdr::ORMH::Slot::mtl)] = metal;
				img_ormh_pixels[i * 4 + static_cast<uint8_t>(rdr::ORMH::Slot::hgt)] = 255;
			}

			Asset<ImageResource>& asset = m_image_bank.add(ormh_key, std::move(img_res_ormh));
			material_res.textures[tex_ormh] = asset.handle;
			material_res.map_mask |= (1U << tex_ormh);
		}

		material_res.uv_index[tex_ormh] =
			mr_source_tex ? static_cast<int8_t>(material.pbr_metallic_roughness.metallic_roughness_texture.texcoord) : static_cast<int8_t>(material.occlusion_texture.texcoord);
	}

	if (material.has_pbr_metallic_roughness) {
		const auto& gltf_pbr = material.pbr_metallic_roughness;

		material_res.albedo_tint =
			glm::make_vec4(gltf_pbr.base_color_factor);
		material_res.metallic_factor =
			static_cast<float>(gltf_pbr.metallic_factor);
		material_res.roughness_factor =
			static_cast<float>(gltf_pbr.roughness_factor);

		if (gltf_pbr.base_color_texture.texture) {
			assign_tex(gltf_pbr.base_color_texture.texture, tex_albedo);
			material_res.uv_index[tex_albedo] =
				static_cast<int8_t>(
					gltf_pbr.base_color_texture.texcoord
				);
		}
	}

	if (material.normal_texture.texture) {
		assign_tex(material.normal_texture.texture, tex_normal);
		material_res.uv_index[tex_normal] =
			static_cast<int8_t>(material.normal_texture.texcoord);
		material_res.normal_scale =
			static_cast<float>(material.normal_texture.scale);
	}

	if (material.emissive_texture.texture) {
		assign_tex(material.emissive_texture.texture, tex_emissive);
		material_res.uv_index[tex_emissive] =
			static_cast<int8_t>(material.emissive_texture.texcoord);
		material_res.emissive_factor =
			glm::make_vec3(material.emissive_factor);
	}

	return material_res;
}


Handle<ImageResource> AssetKeeper::import_gltf_image(
	const cgltf_texture* gltf_texture,
	const char*          gltf_path,
	const cgltf_data*    gltf_root
)
{
	auto key_opt = make_gltf_image_key(gltf_texture, gltf_path, gltf_root);
	if (!key_opt)
		return Handle<ImageResource>::null();

	const char* key = key_opt->data();

	if (auto* existing_image = m_image_bank.find(key))
		return existing_image->handle;

	ImageResource image =
		make_gltf_image(gltf_texture, gltf_path, gltf_root);

	if (image.width == 0 || image.height == 0 || image.channels == 0)
		return Handle<ImageResource>::null();

	Asset<ImageResource>& asset =
		m_image_bank.add(key, std::move(image));

	return asset.handle;
}


ImageResource AssetKeeper::make_gltf_image(
	const cgltf_texture* gltf_texture,
	const char*          gltf_path,
	const cgltf_data*    gltf_root
)
{
	ImageResource image {};

	if (!gltf_texture || !gltf_texture->image) {
		HPR_ERROR(log::LogCategory::asset, "[make_gltf_image] cgltf texture invalid");
		return image;
	}

	auto key_opt = make_gltf_image_key(gltf_texture, gltf_path, gltf_root);
	if (!key_opt)
		return image;

	const char* key = key_opt->data();

	constexpr int channels_expect = 4;
	const cgltf_image* gltf_image = gltf_texture->image;

	if (gltf_image->uri && gltf_image->buffer_view == nullptr) {

		int width  = 0;
		int height = 0;
		int channels_file = 0;

		stbi_uc* pixels_rgba =
			stbi_load(key, &width, &height, &channels_file, channels_expect);

		HPR_DEBUG(
			log::LogCategory::asset,
			"[make_gltf_image][stbi_load][key %s][%dx%d][chs %d][%p]",
			key,
			width,
			height,
			channels_file,
			static_cast<void*>(pixels_rgba)
		);

		if (!pixels_rgba) {
			HPR_ERROR(
				log::LogCategory::asset,
				"[make_gltf_image] stbi_load failed"
			);
			return image;
		}

		image.width    = static_cast<uint32_t>(width);
		image.height   = static_cast<uint32_t>(height);
		image.channels = static_cast<uint32_t>(channels_expect);

		const size_t expected_size =
			static_cast<size_t>(width)  *
			static_cast<size_t>(height) *
			static_cast<size_t>(channels_expect);

		HPR_DEBUG(
			log::LogCategory::asset,
			"[make_gltf_image][before resize][bytes %zu]",
			expected_size
		);

		image.pixels.resize(expected_size);

		HPR_DEBUG(
			log::LogCategory::asset,
			"[make_gltf_image][after resize][data %p][size %zu][cap %zu]",
			static_cast<void*>(image.pixels.data()),
			image.pixels.size(),
			image.pixels.capacity()
		);

		const size_t expected_bytes =
			static_cast<size_t>(width)  *
			static_cast<size_t>(height) *
			static_cast<size_t>(channels_expect);

		HPR_DEBUG(
			log::LogCategory::asset,
			"[make_gltf_image][pixels resize][expected %zu][bytes %zu]",
			image.pixels.size(),
			expected_bytes,
			static_cast<void*>(image.pixels.data())
		);

		HPR_ASSERT_MSG(
			image.pixels.size() == expected_bytes,
			"image byte size mismatch"
		);

		std::memcpy(image.pixels.data(), pixels_rgba, image.pixels.size());
		stbi_image_free(pixels_rgba);
		return image;
	}

	if (gltf_image->buffer_view               &&
		gltf_image->buffer_view->buffer       &&
		gltf_image->buffer_view->buffer->data &&
		gltf_root) {

		const size_t offset = gltf_image->buffer_view->offset;
		const size_t size   = gltf_image->buffer_view->size;
		const size_t total  = gltf_image->buffer_view->buffer->size;

		HPR_ASSERT_MSG(
			offset + size <= total,
			"gltf buffer view oob"
		);

		const uint8_t* blob_ptr =
			static_cast<const uint8_t*>(
				gltf_image->buffer_view->buffer->data
			) + gltf_image->buffer_view->offset;

		HPR_ASSERT_MSG(
			gltf_image->buffer_view->size <= std::numeric_limits<int>::max(),
			"buffer view too large for stbi"
		);

		const int blob_size =
			static_cast<int>(gltf_image->buffer_view->size);

		int width  = 0;
		int height = 0;
		int channels_file = 0;

		stbi_uc* pixels_rgba =
			stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(blob_ptr),
				blob_size,
				&width,
				&height,
				&channels_file,
				channels_expect
			);

		if (!pixels_rgba) {
			HPR_ERROR(log::LogCategory::asset, "[make_gltf_image] stbi_load_from_memory failed");
			return image;
		}

		image.width    = static_cast<uint32_t>(width);
		image.height   = static_cast<uint32_t>(height);
		image.channels = static_cast<uint32_t>(channels_expect);
		image.pixels.resize(
			static_cast<size_t>(width)  *
			static_cast<size_t>(height) *
			static_cast<size_t>(channels_expect)
		);

		std::memcpy(image.pixels.data(), pixels_rgba, image.pixels.size());
		stbi_image_free(pixels_rgba);
		return image;
	}

	return image;
}


Handle<ImageResource> AssetKeeper::add_memory_image(
	const char*              key,
	uint32_t                 width,
	uint32_t                 height,
	uint32_t                 channels,
	std::span<const uint8_t> bytes
)
{
	if (!key || key[0] == '\0')
		return Handle<ImageResource>::null();

	if (width == 0 || height == 0 || channels == 0)
		return Handle<ImageResource>::null();

	const size_t size_expect =
		static_cast<size_t>(width)  *
		static_cast<size_t>(height) *
		static_cast<size_t>(channels);

	if (bytes.size() != size_expect)
		return Handle<ImageResource>::null();

	if (auto existing_asset = m_image_bank.find(key)) {
		return existing_asset->handle;
	}

	ImageResource image {};
	image.width    = width;
	image.height   = height;
	image.channels = channels;

	image.pixels.resize(size_expect);
	std::memcpy(image.pixels.data(), bytes.data(), size_expect);

	return m_image_bank.add(key, std::move(image)).handle;
}


std::optional<AssetKeeper::TextureKey> AssetKeeper::make_gltf_image_key(
	const cgltf_texture* gltf_texture,
	const char*          gltf_path,
	const cgltf_data*    gltf_root
)
{
	TextureKey key {};

	if (!gltf_texture || !gltf_texture->image)
		return std::nullopt;

	const cgltf_image* gltf_image = gltf_texture->image;

	if (gltf_image->uri && gltf_image->buffer_view == nullptr) {
		const char* slash_fwd = std::strrchr(gltf_path, '/');
		const char* slash_bck = std::strrchr(gltf_path, '\\');
		const char* separator = slash_fwd ? slash_fwd : slash_bck;
		const size_t base_len =
			separator
			? static_cast<size_t>(separator - gltf_path + 1)
			: 0;

		char resolved_path[cfg::max_path_length] {0};
		const size_t uri_len = std::strlen(gltf_image->uri);

		if (base_len + uri_len + 1 < sizeof(resolved_path)) {
			std::memcpy(resolved_path, gltf_path, base_len);
			std::memcpy(resolved_path + base_len, gltf_image->uri, uri_len);
			resolved_path[base_len + uri_len] = '\0';
		}

		const char* file_path =
			resolved_path[0] ? resolved_path : gltf_image->uri;

		std::snprintf(key.data(), key.size(), "%s", file_path);
		return key;
	}

	if (gltf_image->buffer_view &&
		gltf_image->buffer_view->buffer &&
		gltf_image->buffer_view->buffer->data &&
		gltf_root) {

		uint32_t image_index = 0;
		for (cgltf_size i = 0; i < gltf_root->images_count; ++i) {
			if (&gltf_root->images[i] == gltf_image) {
				image_index = static_cast<uint32_t>(i);
				break;
			}
		}

		int key_length =
			std::snprintf(
				key.data(),
				key.size(),
				"%s#image/%u",
				gltf_path ? gltf_path : "",
				image_index
			);

		if (key_length <= 0 ||
			key_length >= static_cast<int>(key.size()))
			return std::nullopt;

		return key;
	}

	return std::nullopt;
}


} // hpr::res

