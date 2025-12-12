#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "cgltf.h"

#include "mtp_memory.hpp"

#include "asset_bank.hpp"
#include "asset_data.hpp"


namespace hpr::res {


template<typename T>
concept supported_asset =
	std::is_same_v<T, ImageResource> ||
	std::is_same_v<T, GltfResource>  ||
	std::is_same_v<T, MaterialResource>;


class AssetKeeper
{
public:

	AssetKeeper() = default;
	~AssetKeeper();

	AssetKeeper(const AssetKeeper&) = delete;
	AssetKeeper& operator=(const AssetKeeper&) = delete;
	AssetKeeper(AssetKeeper&&) noexcept = default;
	AssetKeeper& operator=(AssetKeeper&&) noexcept = delete;


	ImportModel import_gltf_model(const char* path);


	template<supported_asset T>
	AssetBank<T>& storage()
	{
		if constexpr (std::is_same_v<T, ImageResource>) {
			return m_image_bank;
		}
		else if constexpr (std::is_same_v<T, GltfResource>) {
			return m_gltf_bank;
		}
		else if constexpr (std::is_same_v<T, MaterialResource>) {
			return m_material_template_bank;
		}
	}

	template<supported_asset T>
	const AssetBank<T>& storage() const
	{
		if constexpr (std::is_same_v<T, ImageResource>) {
			return m_image_bank;
		}
		else if constexpr (std::is_same_v<T, GltfResource>) {
			return m_gltf_bank;
		}
		else if constexpr (std::is_same_v<T, MaterialResource>) {
			return m_material_template_bank;
		}
	}

private:

	struct VertexAccessor
	{
		const cgltf_accessor* position {nullptr};
		const cgltf_accessor* normal   {nullptr};
		const cgltf_accessor* tangent  {nullptr};
		const cgltf_accessor* uv0      {nullptr};
		const cgltf_accessor* uv1      {nullptr};
		const cgltf_accessor* color    {nullptr};
		const cgltf_accessor* extra    {nullptr};
	};

	Handle<GltfResource> load_gltf(const char* path);

	VertexAccessor extract_vertex_attributes(const cgltf_primitive& primitive, uint32_t uv_index);
	void extract_vertex_geometry(const VertexAccessor& accessor, ImportPrimitiveGeometry& geometry);
	void extract_index_data(const cgltf_primitive& primitive, ImportPrimitiveGeometry& geometry);

	Handle<MaterialResource> import_gltf_material(
		const char*            gltf_path,
		const cgltf_data*      gltf_root,
		const cgltf_primitive& gltf_primitive);

	MaterialResource make_gltf_material(
		const cgltf_material& material,
		const char*           gltf_path,
		const cgltf_data*     gltf_root);

	Handle<ImageResource> import_gltf_image(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root);

	ImageResource make_gltf_image(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root);

private:

	inline static constexpr uint32_t k_max_path_length = 1024;

	using TextureKey = std::array<char, k_max_path_length>;

	static std::optional<TextureKey> make_gltf_image_key(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root);

	inline static constexpr uint64_t k_fnv_offset = 1469598103934665603ULL;
	inline static constexpr uint64_t k_fnv_prime  = 1099511628211ULL;
	inline static constexpr uint64_t k_mix_phi64  = 0x9E3779B97F4A7C15ULL;
	inline static constexpr uint64_t k_mix_mm364  = 0xC2B2AE3D27D4EB4FULL;
	inline static constexpr uint64_t k_mix_sm64a  = 0xBF58476D1CE4E5B9ULL;
	inline static constexpr uint64_t k_mix_sm64b  = 0x94D049BB133111EBULL;

	static constexpr uint8_t float_to_unorm8(float value)
	{
		if (value < 0.0f) value = 0.0f;
		else if (value > 1.0f) value = 1.0f;
		return static_cast<uint8_t>(value * 255.0f + 0.5f);
	}

	static constexpr uint64_t make_stream_key(const char* path, uint32_t mesh_index, uint32_t primitive_index, uint32_t kind) noexcept
	{
		uint64_t hash = k_fnv_offset;
		if (path) {
			for (const unsigned char* prime = reinterpret_cast<const unsigned char*>(path); *prime; ++prime) {
				hash = (hash ^ *prime) * k_fnv_prime;
			}
		}
		hash ^= k_mix_phi64 + static_cast<uint64_t>(mesh_index)      + (hash << 6) + (hash >> 2);
		hash ^= k_mix_sm64a + static_cast<uint64_t>(primitive_index) + (hash << 6) + (hash >> 2);
		hash ^= k_mix_sm64b + static_cast<uint64_t>(kind)            + (hash << 6) + (hash >> 2);
		return hash;
	}

private:

	AssetBank<ImageResource>    m_image_bank;
	AssetBank<GltfResource>     m_gltf_bank;
	AssetBank<MaterialResource> m_material_template_bank;
};

} // hpr::res

