#pragma once

#include <cstdint>

#include "cgltf.h"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "render_data.hpp"


namespace hpr::res {


struct ImageResource
{
	uint32_t width    {0};
	uint32_t height   {0};
	uint32_t channels {0};

	mtp::vault<uint8_t, mtp::default_set> pixels;
};


struct GltfResource
{
	cgltf_data* data {nullptr};
};


enum : uint32_t {

	Tex_Albedo   = 0,
	Tex_Normal   = 1,
	Tex_ORMH     = 2,
	Tex_Emissive = 3,
	MaxTexPerMat = 4
};

struct MaterialResource
{
	vec4  albedo_tint      {1.0f, 1.0f, 1.0f, 1.0f};
	float metallic_factor  {1.0f};
	float roughness_factor {1.0f};
	float ao_strength      {1.0f};
	float normal_scale     {1.0f};
	vec3  emissive_factor  {0.0f, 0.0f, 0.0f};

	uint32_t map_mask {0};
	int8_t   uv_index[MaxTexPerMat] {0, 0, 0, 0};

	Handle<ImageResource> textures[MaxTexPerMat];

	bool has_albedo()   const { return (map_mask & (1U << Tex_Albedo))   != 0U; }
	bool has_normal()   const { return (map_mask & (1U << Tex_Normal))   != 0U; }
	bool has_ormh()     const { return (map_mask & (1U << Tex_ORMH))     != 0U; }
	bool has_emissive() const { return (map_mask & (1U << Tex_Emissive)) != 0U; }
};


struct ImportPrimitiveGeometry
{
	mtp::vault<uint8_t, mtp::default_set> vtx_bytes;
	mtp::vault<uint8_t, mtp::default_set> idx_bytes;

	uint32_t vtx_count     {0};
	uint32_t idx_count     {0};
	uint64_t vtx_buf_key   {0};
	uint64_t idx_buf_key   {0};
	uint32_t submesh_index {0};
};

struct ImportPrimitive
{
	ImportPrimitiveGeometry geometry;
	Handle<res::MaterialResource> material_template;
};


struct ImportModel
{
	mtp::vault<ImportPrimitive, mtp::default_set> primitives;
};

} // hpr::res

