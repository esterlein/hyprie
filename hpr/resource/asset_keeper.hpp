#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "cgltf.h"

#include "mtp_memory.hpp"

#include "asset_bank.hpp"
#include "asset_data.hpp"


namespace hpr::res {


namespace cfg {

	inline constexpr uint32_t max_path_length = 1024U;

} // hpr::res::cfg


template<typename T>
concept supported_asset =
	std::is_same_v<T, ImageResource>    ||
	std::is_same_v<T, GltfResource>     ||
	std::is_same_v<T, MaterialResource> ||
	std::is_same_v<T, ImportModel>;


class AssetKeeper
{
public:

	AssetKeeper() = default;
	~AssetKeeper();

	AssetKeeper(const AssetKeeper&) = delete;
	AssetKeeper& operator=(const AssetKeeper&) = delete;
	AssetKeeper(AssetKeeper&&) noexcept = default;
	AssetKeeper& operator=(AssetKeeper&&) = delete;

	Handle<ImportModel> import_gltf_model(const char* path);

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
		else if constexpr (std::is_same_v<T, ImportModel>) {
			return m_model_bank;
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
		else if constexpr (std::is_same_v<T, ImportModel>) {
			return m_model_bank;
		}
	}

private:

	struct VtxAccessors
	{
		const cgltf_accessor* pos {nullptr};
		const cgltf_accessor* nrm {nullptr};
		const cgltf_accessor* tan {nullptr};
		const cgltf_accessor* uv0 {nullptr};
		const cgltf_accessor* uv1 {nullptr};
		const cgltf_accessor* rgb {nullptr};
		const cgltf_accessor* ext {nullptr};
		const cgltf_accessor* jnt {nullptr};
		const cgltf_accessor* wgt {nullptr};
	};

	Handle<GltfResource> load_gltf(const char* path);

	void allocate_mesh_memory(
		const cgltf_mesh& gltf_mesh,
		ImportMesh&       out_mesh
	);

	VtxAccessors extract_vtx_accessors(
		const cgltf_primitive& primitive
	);

	void extract_vtx_attribute(
		const cgltf_accessor*                  accessor,
		mtp::vault<uint8_t, mtp::default_set>& mesh_attrib_vlt,
		uint32_t                               start_idx,
		uint32_t                               count
	);

	uint32_t extract_mesh_indices(
		const cgltf_primitive&                  primitive,
		mtp::vault<uint32_t, mtp::default_set>& indices,
		uint32_t                                idx_cursor,
		uint32_t                                vtx_cursor
	);

	Handle<MaterialResource> import_gltf_material(
		const char*           gltf_path,
		const cgltf_data*     gltf_root,
		const cgltf_material* gltf_material
	);

	MaterialResource make_gltf_material(
		const cgltf_material& material,
		const char*           gltf_path,
		const cgltf_data*     gltf_root
	);

	Handle<ImageResource> import_gltf_image(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root
	);

	ImageResource make_gltf_image(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root
	);

	ImageResource make_hdr_image(const char* path);

public:

	Handle<ImageResource> import_hdr_image(const char* path);

	Handle<ImageResource> add_memory_image(
		const char*              key,
		uint32_t                 width,
		uint32_t                 height,
		uint32_t                 channels,
		std::span<const uint8_t> bytes
	);

private:

	using TextureKey = std::array<char, cfg::max_path_length>;

	static std::optional<TextureKey> make_gltf_image_key(
		const cgltf_texture* gltf_texture,
		const char*          gltf_path,
		const cgltf_data*    gltf_root
	);

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

	static constexpr uint64_t make_stream_key(
		const char* path,
		uint32_t    mesh_index,
		uint32_t    primitive_index,
		uint32_t    kind
	) noexcept
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
	AssetBank<ImportModel>      m_model_bank;

	std::unordered_map<std::string, Handle<ImportModel>> m_model_cache;
};

} // hpr::res

