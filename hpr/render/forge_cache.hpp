#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "sokol_gfx.h"
#include "mtp_memory.hpp"

#include "handle.hpp"
#include "render_data.hpp"


namespace hpr::rdr {


struct MeshKey
{
	uint32_t vtx_buf_id;
	uint32_t idx_buf_id;
	uint32_t vtx_count;
	uint32_t idx_count;

	bool operator==(const MeshKey& other) const
	{
		return vtx_buf_id == other.vtx_buf_id
			&& idx_buf_id == other.idx_buf_id
			&& vtx_count  == other.vtx_count
			&& idx_count  == other.idx_count;
	}
};

struct MatKey
{
	uint64_t path_hash;
	uint32_t material_index;
	uint32_t shader_index;
	uint32_t import_options;

	bool operator==(const MatKey& other) const
	{
		return path_hash      == other.path_hash
			&& material_index == other.material_index
			&& shader_index   == other.shader_index
			&& import_options == other.import_options;
	}
};

struct TexKey
{
	uint64_t source_key;
	uint32_t sampler_key;
	uint8_t  srgb;

	bool operator==(const TexKey& other) const
	{
		return source_key  == other.source_key
			&& sampler_key == other.sampler_key
			&& srgb        == other.srgb;
	}
};


static inline constexpr uint64_t k_fnv_offset = 1469598103934665603ULL;
static inline constexpr uint64_t k_fnv_prime  = 1099511628211ULL;

} // hpr::rdr


// TODO: replace that when mtp::hashmap is done

namespace std {


template<>
struct hash<hpr::rdr::MeshKey>
{
	size_t operator()(const hpr::rdr::MeshKey& mesh_key) const noexcept
	{
		uint64_t hash_value = hpr::rdr::k_fnv_offset;

		auto mix = [&hash_value](uint64_t word)
		{
			hash_value ^= word;
			hash_value *= hpr::rdr::k_fnv_prime;
		};

		mix(static_cast<uint64_t>(mesh_key.vtx_buf_id));
		mix(static_cast<uint64_t>(mesh_key.idx_buf_id));
		mix(static_cast<uint64_t>(mesh_key.vtx_count));
		mix(static_cast<uint64_t>(mesh_key.idx_count));

		return static_cast<size_t>(hash_value);
	}
};

template<>
struct hash<hpr::rdr::MatKey>
{
	size_t operator()(const hpr::rdr::MatKey& material_key) const noexcept
	{
		uint64_t hash_value = hpr::rdr::k_fnv_offset;

		auto mix = [&hash_value](uint64_t word)
		{
			hash_value ^= word;
			hash_value *= hpr::rdr::k_fnv_prime;
		};

		mix(material_key.path_hash);
		mix(static_cast<uint64_t>(material_key.material_index));
		mix(static_cast<uint64_t>(material_key.shader_index));
		mix(static_cast<uint64_t>(material_key.import_options));

		return static_cast<size_t>(hash_value);
	}
};

template<>
struct hash<hpr::rdr::TexKey>
{
	size_t operator()(const hpr::rdr::TexKey& texture_key) const noexcept
	{
		uint64_t hash_value = hpr::rdr::k_fnv_offset;

		auto mix = [&hash_value](uint64_t word)
		{
			hash_value ^= word;
			hash_value *= hpr::rdr::k_fnv_prime;
		};

		mix(texture_key.source_key);
		mix(static_cast<uint64_t>(texture_key.sampler_key));
		mix(static_cast<uint64_t>(texture_key.srgb));

		return static_cast<size_t>(hash_value);
	}
};

} // std


namespace hpr::rdr
{

class ForgeCache
{
public:

	ForgeCache() = default;

	using BufferMap  = decltype(mtp::make_unordered_map<uint64_t, sg_buffer,                mtp::default_set>());
	using MeshMap    = decltype(mtp::make_unordered_map<MeshKey,  Handle<Mesh>,             mtp::default_set>());
	using MatMap     = decltype(mtp::make_unordered_map<MatKey,   Handle<MaterialInstance>, mtp::default_set>());
	using TextureMap = decltype(mtp::make_unordered_map<TexKey,   Handle<Texture>,          mtp::default_set>());

	static inline constexpr uint64_t k_mix_prime0 = 0x9E3779B97F4A7C15ULL;
	static inline constexpr uint64_t k_mix_prime1 = 0x517CC1B727220A95ULL;
	static inline constexpr uint64_t k_mix_prime2 = 0x94D049BB133111EBULL;

	static inline constexpr uint32_t k_shift_l6   = 6U;
	static inline constexpr uint32_t k_shift_r2   = 2U;

	sg_buffer find_vtx_buffer(uint64_t key) const
	{
		auto iterator = m_vtx_buffer_map.find(key);
		if (iterator == m_vtx_buffer_map.end())
			return {};
		return iterator->second;
	}

	void put_vtx_buffer(uint64_t key, sg_buffer buffer)
	{
		m_vtx_buffer_map.emplace(key, buffer);
	}

	sg_buffer find_idx_buffer(uint64_t key) const
	{
		auto iterator = m_idx_buffer_map.find(key);
		if (iterator == m_idx_buffer_map.end())
			return {};
		return iterator->second;
	}

	void put_idx_buffer(uint64_t key, sg_buffer buffer)
	{
		m_idx_buffer_map.emplace(key, buffer);
	}

	Handle<Mesh> find_mesh(const MeshKey& mesh_key) const
	{
		auto iterator = m_mesh_map.find(mesh_key);
		if (iterator == m_mesh_map.end())
			return {};
		return iterator->second;
	}

	void put_mesh(const MeshKey& mesh_key, Handle<Mesh> mesh_handle)
	{
		m_mesh_map.emplace(mesh_key, mesh_handle);
	}

	Handle<MaterialInstance> find_material(const MatKey& material_key) const
	{
		auto iterator = m_material_map.find(material_key);
		if (iterator == m_material_map.end())
			return {};
		return iterator->second;
	}

	void put_material(const MatKey& material_key, Handle<MaterialInstance> material_handle)
	{
		m_material_map.emplace(material_key, material_handle);
	}

	Handle<Texture> find_texture(const TexKey& texture_key) const
	{
		auto iterator = m_texture_map.find(texture_key);
		if (iterator == m_texture_map.end())
			return {};
		return iterator->second;
	}

	void put_texture(const TexKey& texture_key, Handle<Texture> texture_handle)
	{
		m_texture_map.emplace(texture_key, texture_handle);
	}

	static inline uint64_t fnv1a64(const void* data, size_t byte_count)
	{
		uint64_t hash_value = k_fnv_offset;
		const uint8_t* data_bytes = static_cast<const uint8_t*>(data);

		for (size_t byte_index = 0; byte_index < byte_count; ++byte_index)
			hash_value = (hash_value ^ data_bytes[byte_index]) * k_fnv_prime;

		return hash_value;
	}

	static inline uint64_t hash_path_prim_kind(const char* path_cstr, uint32_t primitive_index, uint32_t import_options, uint32_t kind)
	{
		const size_t   path_length = path_cstr ? std::strlen(path_cstr) : static_cast<size_t>(0);
		uint64_t       hash_value  = fnv1a64(path_cstr, path_length);

		hash_value ^= k_mix_prime0 + static_cast<uint64_t>(primitive_index) + (hash_value << k_shift_l6) + (hash_value >> k_shift_r2);
		hash_value ^= k_mix_prime1 + static_cast<uint64_t>(import_options)  + (hash_value << k_shift_l6) + (hash_value >> k_shift_r2);
		hash_value ^= k_mix_prime2 + static_cast<uint64_t>(kind)            + (hash_value << k_shift_l6) + (hash_value >> k_shift_r2);

		return hash_value;
	}

private:

	BufferMap  m_vtx_buffer_map = mtp::make_unordered_map<uint64_t, sg_buffer,                mtp::default_set>();
	BufferMap  m_idx_buffer_map = mtp::make_unordered_map<uint64_t, sg_buffer,                mtp::default_set>();
	MeshMap    m_mesh_map       = mtp::make_unordered_map<MeshKey,  Handle<Mesh>,             mtp::default_set>();
	MatMap     m_material_map   = mtp::make_unordered_map<MatKey,   Handle<MaterialInstance>, mtp::default_set>();
	TextureMap m_texture_map    = mtp::make_unordered_map<TexKey,   Handle<Texture>,          mtp::default_set>();
};

} // hpr::rdr

