#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "stb_image.h"
#include "mtp_memory.hpp"

#include "asset_data.hpp"
#include "asset_keeper.hpp"
#include "log.hpp"


namespace hpr::res {


AssetKeeper::~AssetKeeper()
{
	// TODO: free all
}


ImportModel AssetKeeper::import_gltf_model(const char* path)
{
	ImportModel import_model {};

	Handle<GltfResource> gltf_handle = load_gltf(path);

	const GltfResource* gltf_resource = m_gltf_bank.find(gltf_handle);
	if (!gltf_resource || !gltf_resource->data) {
		HPR_ERROR(
			log::LogCategory::asset,
			"[asset][import_gltf_model] failed to load gltf [path %s]",
			path
		);
		return import_model;
	}

	const cgltf_data* gltf_data = gltf_resource->data;

	size_t primitives_total = 0;
	for (cgltf_size node_index = 0; node_index < gltf_data->nodes_count; ++node_index) {
		const cgltf_node& gltf_node = gltf_data->nodes[node_index];
		if (gltf_node.mesh) {
			primitives_total += static_cast<size_t>(gltf_node.mesh->primitives_count);
		}
	}

	import_model.primitives.reserve(primitives_total);

	for (cgltf_size node_index = 0; node_index < gltf_data->nodes_count; ++node_index) {
		const cgltf_node& gltf_node = gltf_data->nodes[node_index];
		if (!gltf_node.mesh)
			continue;

		const cgltf_mesh& gltf_mesh = *gltf_node.mesh;
		const uint32_t mesh_index = static_cast<uint32_t>(&gltf_mesh - gltf_data->meshes);

		for (cgltf_size primitive_index = 0; primitive_index < gltf_mesh.primitives_count; ++primitive_index) {
			const cgltf_primitive& gltf_primitive = gltf_mesh.primitives[primitive_index];

			const int32_t albedo_uv =
				(gltf_primitive.material && gltf_primitive.material->has_pbr_metallic_roughness)
				? static_cast<int32_t>(gltf_primitive.material->pbr_metallic_roughness.base_color_texture.texcoord)
				: 0;

			VertexAccessor vtx_accessor =
				extract_vertex_attributes(gltf_primitive, static_cast<uint32_t>(albedo_uv));

			if (!vtx_accessor.position)
				continue;

			ImportPrimitive import_primitive {};
			auto& geometry = import_primitive.geometry;

			extract_vertex_geometry(vtx_accessor, geometry);
			extract_index_data(gltf_primitive, geometry);

			geometry.vtx_buf_key =
				make_stream_key(path, mesh_index, static_cast<uint32_t>(primitive_index), 0U);
			geometry.idx_buf_key =
				geometry.idx_count
				? make_stream_key(path, mesh_index, static_cast<uint32_t>(primitive_index), 1U)
				: 0ULL;
			geometry.submesh_index = static_cast<uint32_t>(primitive_index);

			import_primitive.material_template =
				import_gltf_material(path, gltf_data, gltf_primitive);

			import_model.primitives.emplace_back(std::move(import_primitive));
		}
	}

	return import_model;
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

	cgltf_validate(data);

	Asset<GltfResource>& asset = m_gltf_bank.add(path, data);
	return asset.handle;
}


AssetKeeper::VertexAccessor AssetKeeper::extract_vertex_attributes(
	const cgltf_primitive& primitive,
	uint32_t               uv_index
)
{
	VertexAccessor vtx_accessor {};

	for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {

		const cgltf_attribute& attribute = primitive.attributes[i];

		if (attribute.type == cgltf_attribute_type_position) {
			vtx_accessor.position = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_normal) {
			vtx_accessor.normal = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_tangent) {
			vtx_accessor.tangent = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == uv_index) {
			vtx_accessor.uv0 = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_color && attribute.index == 0) {
			vtx_accessor.color = attribute.data;
		}
	}

	if (!vtx_accessor.uv0) {
		for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
			if (primitive.attributes[i].type == cgltf_attribute_type_texcoord) {
				vtx_accessor.uv0 = primitive.attributes[i].data;
				break;
			}
		}
	}

	for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
		const cgltf_attribute& attribute = primitive.attributes[i];
		if (attribute.type == cgltf_attribute_type_texcoord &&
			attribute.data != vtx_accessor.uv0) {
			vtx_accessor.uv1 = attribute.data;
			break;
		}
	}

	if (!vtx_accessor.uv1) {
		vtx_accessor.uv1 = vtx_accessor.uv0;
	}

	for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
		const cgltf_attribute& attribute = primitive.attributes[i];
		if (attribute.type == cgltf_attribute_type_invalid &&
			attribute.name &&
			(std::strcmp(attribute.name, "EXTRA") == 0 ||
			 std::strcmp(attribute.name, "EXTRA_0") == 0)) {
			vtx_accessor.extra = attribute.data;
			break;
		}
	}

	return vtx_accessor;
}


void AssetKeeper::extract_vertex_geometry(
	const VertexAccessor&       accessors,
	ImportPrimitiveGeometry&    geometry
)
{
	if (!accessors.position) {
		HPR_ERROR(
			log::LogCategory::asset,
			"[asset][extract_vertex_geometry] missing position accessor"
		);
		return;
	}

	const uint32_t vertex_count =
		static_cast<uint32_t>(accessors.position->count);

	const bool has_normal  = accessors.normal  && (accessors.normal->count  == accessors.position->count);
	const bool has_tangent = accessors.tangent && (accessors.tangent->count == accessors.position->count);
	const bool has_uv0     = accessors.uv0     && (accessors.uv0->count     == accessors.position->count);
	const bool has_uv1     = accessors.uv1     && (accessors.uv1->count     == accessors.position->count);
	const bool has_color   = accessors.color   && (accessors.color->count   == accessors.position->count);
	const bool has_extra   = accessors.extra   && (accessors.extra->count   == accessors.position->count);

	constexpr size_t stride_bytes     = 64;
	constexpr size_t offset_tangent   = 0;
	constexpr size_t offset_position  = 16;
	constexpr size_t offset_normal    = 28;
	constexpr size_t offset_uv0       = 40;
	constexpr size_t offset_uv1       = 48;
	constexpr size_t tail_offset      = 56;
	constexpr size_t tail_color_bytes = 4;
	constexpr size_t tail_extra_bytes = 4;

	geometry.vtx_count = vertex_count;
	geometry.vtx_bytes.resize(static_cast<size_t>(vertex_count) * stride_bytes);

	for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
		vec4 position {0.0f, 0.0f, 0.0f, 1.0f};
		cgltf_accessor_read_float(
			accessors.position,
			vertex_index,
			glm::value_ptr(position),
			glm::vec4::length()
		);

		vec4 normal {0.0f, 0.0f, 1.0f, 0.0f};
		if (has_normal) {
			cgltf_accessor_read_float(
				accessors.normal,
				vertex_index,
				glm::value_ptr(normal),
				glm::vec3::length()
			);
		}

		vec4 tangent {1.0f, 0.0f, 0.0f, 1.0f};
		if (has_tangent) {
			cgltf_accessor_read_float(
				accessors.tangent,
				vertex_index,
				glm::value_ptr(tangent),
				glm::vec4::length()
			);
		}

		vec2 uv0 {0.0f, 0.0f};
		if (has_uv0) {
			cgltf_accessor_read_float(
				accessors.uv0,
				vertex_index,
				glm::value_ptr(uv0),
				glm::vec2::length()
			);
		}

		vec2 uv1 = uv0;
		if (has_uv1) {
			cgltf_accessor_read_float(
				accessors.uv1,
				vertex_index,
				glm::value_ptr(uv1),
				glm::vec2::length()
			);
		}

		uint8_t* base_ptr =
			geometry.vtx_bytes.data() +
			static_cast<size_t>(vertex_index) * stride_bytes;

		float* tangent_dst =
			reinterpret_cast<float*>(base_ptr + offset_tangent);
		std::memcpy(
			tangent_dst,
			glm::value_ptr(tangent),
			glm::vec4::length() * sizeof(float)
		);

		float* position_dst =
			reinterpret_cast<float*>(base_ptr + offset_position);
		const vec3 position3 = vec3(position);
		std::memcpy(
			position_dst,
			glm::value_ptr(position3),
			glm::vec3::length() * sizeof(float)
		);

		float* normal_dst =
			reinterpret_cast<float*>(base_ptr + offset_normal);
		const vec3 normal3 = vec3(normal);
		std::memcpy(
			normal_dst,
			glm::value_ptr(normal3),
			glm::vec3::length() * sizeof(float)
		);

		float* uv0_dst =
			reinterpret_cast<float*>(base_ptr + offset_uv0);
		std::memcpy(
			uv0_dst,
			glm::value_ptr(uv0),
			glm::vec2::length() * sizeof(float)
		);

		float* uv1_dst =
			reinterpret_cast<float*>(base_ptr + offset_uv1);
		std::memcpy(
			uv1_dst,
			glm::value_ptr(uv1),
			glm::vec2::length() * sizeof(float)
		);

		uint8_t* tail_ptr = base_ptr + tail_offset;

		vec4 color_linear {1.0f, 1.0f, 1.0f, 1.0f};
		if (has_color) {
			cgltf_accessor_read_float(
				accessors.color,
				vertex_index,
				glm::value_ptr(color_linear),
				glm::vec4::length()
			);
		}

		uint32_t color_packed =
			(static_cast<uint32_t>(float_to_unorm8(color_linear[0])))       |
			(static_cast<uint32_t>(float_to_unorm8(color_linear[1])) <<  8) |
			(static_cast<uint32_t>(float_to_unorm8(color_linear[2])) << 16) |
			(static_cast<uint32_t>(float_to_unorm8(color_linear[3])) << 24);

		std::memcpy(
			tail_ptr + 0,
			&color_packed,
			tail_color_bytes
		);

		uint32_t extra_packed = 0;
		if (has_extra) {
			std::array<uint32_t, 4> extra_attribute {0, 0, 0, 0};
			cgltf_accessor_read_uint(
				accessors.extra,
				vertex_index,
				extra_attribute.data(),
				extra_attribute.size()
			);
			extra_packed = extra_attribute[0];
		}

		std::memcpy(
			tail_ptr + tail_color_bytes,
			&extra_packed,
			tail_extra_bytes
		);
	}
}


void AssetKeeper::extract_index_data(
	const cgltf_primitive&   primitive,
	ImportPrimitiveGeometry& geometry
)
{
	static constexpr uint32_t min_vertices_for_triangle = 3U;
	static constexpr uint32_t indices_per_triangle      = 3U;

	geometry.idx_count = 0;
	geometry.idx_bytes.clear();

	if (!primitive.indices) {
		HPR_WARN(
			log::LogCategory::asset,
			"[asset][extract_index_data] primitive has no indices"
		);
		return;
	}

	const uint32_t source_index_count =
		static_cast<uint32_t>(primitive.indices->count);

	if (source_index_count == 0) {
		HPR_WARN(
			log::LogCategory::asset,
			"[asset][extract_index_data] index count is zero"
		);
		return;
	}

	auto prepare_index_buffer =
	[](ImportPrimitiveGeometry& geo, uint32_t count) -> uint32_t*
	{
		geo.idx_count = count;
		geo.idx_bytes.resize(static_cast<size_t>(count) * sizeof(uint32_t));
		return reinterpret_cast<uint32_t*>(geo.idx_bytes.data());
	};

	switch (primitive.type) {
	case cgltf_primitive_type_triangles:
	{
		uint32_t* indices_ptr =
			prepare_index_buffer(geometry, source_index_count);

		for (uint32_t read_index = 0; read_index < source_index_count; ++read_index) {
			indices_ptr[read_index] =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index
					)
				);
		}
	}
	break;

	case cgltf_primitive_type_triangle_strip:
	{
		if (source_index_count < min_vertices_for_triangle) {
			HPR_WARN(
				log::LogCategory::asset,
				"[asset][extract_index_data] triangle strip too small"
			);
			return;
		}

		const uint32_t triangle_count =
			source_index_count - (min_vertices_for_triangle - 1U);
		const uint32_t output_index_count =
			triangle_count * indices_per_triangle;

		uint32_t* indices_ptr =
			prepare_index_buffer(geometry, output_index_count);

		uint32_t write_index = 0;
		for (uint32_t read_index = 0;
			 read_index + 2U < source_index_count;
			 ++read_index) {

			const uint32_t i0 =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index + 0U
					)
				);
			const uint32_t i1 =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index + 1U
					)
				);
			const uint32_t i2 =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index + 2U
					)
				);

			if (read_index & 1U) {
				indices_ptr[write_index + 0U] = i1;
				indices_ptr[write_index + 1U] = i0;
				indices_ptr[write_index + 2U] = i2;
			}
			else {
				indices_ptr[write_index + 0U] = i0;
				indices_ptr[write_index + 1U] = i1;
				indices_ptr[write_index + 2U] = i2;
			}
			write_index += indices_per_triangle;
		}
	}
	break;

	case cgltf_primitive_type_triangle_fan:
	{
		if (source_index_count < min_vertices_for_triangle) {
			HPR_WARN(
				log::LogCategory::asset,
				"[asset][extract_index_data] triangle fan too small"
			);
			return;
		}

		const uint32_t triangle_count =
			source_index_count - (min_vertices_for_triangle - 1U);
		const uint32_t output_index_count =
			triangle_count * indices_per_triangle;

		uint32_t* indices_ptr =
			prepare_index_buffer(geometry, output_index_count);

		const uint32_t fan_root =
			static_cast<uint32_t>(
				cgltf_accessor_read_index(
					primitive.indices,
					0U
				)
			);

		uint32_t write_index = 0;
		for (uint32_t read_index = 1U;
			 read_index + 1U < source_index_count;
			 ++read_index) {

			const uint32_t index_0 =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index + 0U
					)
				);
			const uint32_t index_1 =
				static_cast<uint32_t>(
					cgltf_accessor_read_index(
						primitive.indices,
						read_index + 1U
					)
				);

			indices_ptr[write_index + 0U] = fan_root;
			indices_ptr[write_index + 1U] = index_0;
			indices_ptr[write_index + 2U] = index_1;

			write_index += indices_per_triangle;
		}
	}
	break;
	}
}


Handle<MaterialResource> AssetKeeper::import_gltf_material(
	const char*            gltf_path,
	const cgltf_data*      gltf_root,
	const cgltf_primitive& gltf_primitive
)
{
	if (!gltf_primitive.material)
		return Handle<MaterialResource>::null();

	const cgltf_material* gltf_material = gltf_primitive.material;
	const uint32_t material_index =
		static_cast<uint32_t>(gltf_material - gltf_root->materials);

	if (auto* existing_material =
			m_material_template_bank.find_composite(gltf_path, material_index))
		return existing_material->handle;

	MaterialResource material_template =
		make_gltf_material(*gltf_material, gltf_path, gltf_root);

	Asset<MaterialResource>& asset =
		m_material_template_bank.add_composite(
			gltf_path,
			material_index,
			material_template
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
	for (int i = 0; i < MaxTexPerMat; ++i)
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
				Handle<ImageResource> hnd =
					import_gltf_image(tex, gltf_path, gltf_root);
				if (hnd.is_valid()) {
					material_res.textures[slot] = hnd;
					material_res.map_mask |= (1U << slot);
				}
			}
		}
		else {
			Handle<ImageResource> hnd =
				import_gltf_image(tex, gltf_path, gltf_root);
			if (hnd.is_valid()) {
				material_res.textures[slot] = hnd;
				material_res.map_mask |= (1U << slot);
			}
		}
	};

	auto assign_ormh =
	[this, &material_res, &gltf_path, &gltf_root]
	(const cgltf_texture* tex)
	{
		auto key_opt = make_gltf_image_key(tex, gltf_path, gltf_root);
		if (!key_opt)
			return;

		const char* key = key_opt->data();
		if (auto* existing_tex = m_image_bank.find(key)) {
			material_res.textures[Tex_ORMH] = existing_tex->handle;
			material_res.map_mask |= (1U << Tex_ORMH);
			return;
		}

		const ImageResource res_mr =
			make_gltf_image(tex, gltf_path, gltf_root);

		ImageResource res_ormh {};
		res_ormh.width    = res_mr.width;
		res_ormh.height   = res_mr.height;
		res_ormh.channels = 4;

		const size_t pixel_count =
			static_cast<size_t>(res_ormh.width) * res_ormh.height;

		res_ormh.pixels.reserve(pixel_count * 4);

		for (size_t i = 0; i < pixel_count; ++i) {
			uint8_t rough = res_mr.pixels[i * 4 + 1];
			uint8_t metal = res_mr.pixels[i * 4 + 2];

			res_ormh.pixels.emplace_back(255);
			res_ormh.pixels.emplace_back(rough);
			res_ormh.pixels.emplace_back(metal);
			res_ormh.pixels.emplace_back(255);
		}

		Asset<ImageResource>& asset =
			m_image_bank.add(key, std::move(res_ormh));

		material_res.textures[Tex_ORMH] = asset.handle;
		material_res.map_mask |= (1U << Tex_ORMH);
	};

	if (material.has_pbr_metallic_roughness) {
		const auto& gltf_pbr = material.pbr_metallic_roughness;

		material_res.albedo_tint =
			glm::make_vec4(gltf_pbr.base_color_factor);
		material_res.metallic_factor =
			static_cast<float>(gltf_pbr.metallic_factor);
		material_res.roughness_factor =
			static_cast<float>(gltf_pbr.roughness_factor);

		if (gltf_pbr.base_color_texture.texture) {
			assign_tex(gltf_pbr.base_color_texture.texture, Tex_Albedo);
			material_res.uv_index[Tex_Albedo] =
				static_cast<int8_t>(
					gltf_pbr.base_color_texture.texcoord
				);
		}

		if (gltf_pbr.metallic_roughness_texture.texture) {
			assign_ormh(
				gltf_pbr.metallic_roughness_texture.texture
			);
			material_res.uv_index[Tex_ORMH] =
				static_cast<int8_t>(
					gltf_pbr.metallic_roughness_texture.texcoord
				);
		}
	}

	if (material.normal_texture.texture) {
		assign_tex(material.normal_texture.texture, Tex_Normal);
		material_res.uv_index[Tex_Normal] =
			static_cast<int8_t>(material.normal_texture.texcoord);
		material_res.normal_scale =
			static_cast<float>(material.normal_texture.scale);
	}

	if (material.emissive_texture.texture) {
		assign_tex(material.emissive_texture.texture, Tex_Emissive);
		material_res.uv_index[Tex_Emissive] =
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

	if (!gltf_texture || !gltf_texture->image)
		return image;

	auto key_opt = make_gltf_image_key(gltf_texture, gltf_path, gltf_root);
	if (!key_opt)
		return image;

	const char* key = key_opt->data();

	constexpr int num_channels = 4;
	const cgltf_image* gltf_image = gltf_texture->image;

	if (gltf_image->uri && gltf_image->buffer_view == nullptr) {

		int width = 0;
		int height = 0;
		int channels_in_file = 0;

		stbi_uc* pixels_rgba =
			stbi_load(key, &width, &height, &channels_in_file, num_channels);

		if (!pixels_rgba)
			return image;

		image.width    = static_cast<uint32_t>(width);
		image.height   = static_cast<uint32_t>(height);
		image.channels = static_cast<uint32_t>(num_channels);
		image.pixels.resize(
			static_cast<size_t>(width) *
			static_cast<size_t>(height) *
			static_cast<size_t>(num_channels)
		);

		std::memcpy(image.pixels.data(), pixels_rgba, image.pixels.size());
		stbi_image_free(pixels_rgba);
		return image;
	}

	if (gltf_image->buffer_view &&
		gltf_image->buffer_view->buffer &&
		gltf_image->buffer_view->buffer->data &&
		gltf_root) {

		const uint8_t* blob_ptr =
			static_cast<const uint8_t*>(
				gltf_image->buffer_view->buffer->data
			) + gltf_image->buffer_view->offset;

		const int blob_size =
			static_cast<int>(gltf_image->buffer_view->size);

		int width = 0;
		int height = 0;
		int channels_in_file = 0;

		stbi_uc* pixels_rgba =
			stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(blob_ptr),
				blob_size,
				&width,
				&height,
				&channels_in_file,
				num_channels
			);

		if (!pixels_rgba)
			return image;

		image.width    = static_cast<uint32_t>(width);
		image.height   = static_cast<uint32_t>(height);
		image.channels = static_cast<uint32_t>(num_channels);
		image.pixels.resize(
			static_cast<size_t>(width) *
			static_cast<size_t>(height) *
			static_cast<size_t>(num_channels)
		);

		std::memcpy(image.pixels.data(), pixels_rgba, image.pixels.size());
		stbi_image_free(pixels_rgba);
		return image;
	}

	return image;
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

		char resolved_path[k_max_path_length] {0};
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

