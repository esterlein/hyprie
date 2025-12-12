#pragma once

#include <array>
#include <cstdint>

#include "sokol_gfx.h"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"


namespace hpr::rdr {

namespace cfg {

inline constexpr uint8_t max_tex_per_mat = 4;

} // hpr::rdr::cfg


struct SurfaceInfo
{
	uint32_t width  {1};
	uint32_t height {1};
	float    aspect {1.0f};
	float    dpi    {1.0f};

	int sample_count {1};

	sg_pixel_format color_format;
	sg_pixel_format depth_format;
};


struct SceneVertex
{
	vec4 tan;
	vec3 pos;
	vec3 nrm;
	vec2 uv0;
	vec2 uv1;

	uint32_t rgba;
	uint32_t ext;
};

static_assert(sizeof(SceneVertex) == 64);


struct UiVertex
{
	vec2 pos;
	vec2 uv;

	uint32_t col;
};

static_assert(sizeof(UiVertex) == 20);


struct OverlayVertex
{
	vec3 pos;
};

static_assert(sizeof(OverlayVertex) == 12);


struct QuadVertex
{
	vec3 pos;
	vec2 uv;
};

static_assert(sizeof(QuadVertex) == 20);


struct Tex
{
	enum Slot : uint8_t
	{
		alb = 0,
		nrm,
		orm,
		ems,
		cnt
	};

	enum Flag : uint8_t
	{
		albedo   = 1U << alb,
		normal   = 1U << nrm,
		ormh     = 1U << orm,
		emissive = 1U << ems
	};
};

using TexSlot = Tex::Slot;
using MapFlag = Tex::Flag;


enum class ProgramFlag : uint8_t
{
	None = 0,
	Grid          = 1 << 0,
	OutlineMask   = 1 << 1,
	OutlineDilate = 1 << 2,
	OutlineBlend  = 1 << 3
};


enum class ProgramFlagsMode : uint8_t
{
	none,
	pbr_maps
};


struct Program
{
	sg_shader   shader;
	sg_pipeline pipeline;

	std::array<uint8_t, cfg::max_tex_per_mat> image_slots;
	std::array<uint8_t, cfg::max_tex_per_mat> sampler_slots;

	uint8_t flags {0};

	sg_pixel_format color_format {SG_PIXELFORMAT_NONE};
	sg_pixel_format depth_format {SG_PIXELFORMAT_NONE};

	int sample_count {1};

	Program() = delete;
	Program(const Program&) = delete;
	Program& operator=(const Program&) = delete;

	Program(sg_shader shader_in, sg_pipeline pipeline_in) noexcept
		: shader   {shader_in}
		, pipeline {pipeline_in}
	{}

	Program(Program&& other) noexcept
	{
		shader        = other.shader;
		pipeline      = other.pipeline;
		image_slots   = other.image_slots;
		sampler_slots = other.sampler_slots;
		flags         = other.flags;

		color_format  = other.color_format;
		depth_format  = other.depth_format;
		sample_count  = other.sample_count;

		other.shader       = {};
		other.pipeline     = {};
		other.flags        = 0;
		other.color_format = SG_PIXELFORMAT_NONE;
		other.depth_format = SG_PIXELFORMAT_NONE;
		other.sample_count = 1;
	}

	Program& operator=(Program&& other) noexcept
	{
		if (this != &other) {
			if (pipeline.id) {
				sg_destroy_pipeline(pipeline);
			}
			if (shader.id) {
				sg_destroy_shader(shader);
			}

			shader        = other.shader;
			pipeline      = other.pipeline;
			image_slots   = other.image_slots;
			sampler_slots = other.sampler_slots;
			flags         = other.flags;

			color_format  = other.color_format;
			depth_format  = other.depth_format;
			sample_count  = other.sample_count;

			other.shader       = {};
			other.pipeline     = {};
			other.flags        = 0;
			other.color_format = SG_PIXELFORMAT_NONE;
			other.depth_format = SG_PIXELFORMAT_NONE;
			other.sample_count = 1;
		}
		return *this;
	}

	~Program()
	{
		if (pipeline.id) {
			sg_destroy_pipeline(pipeline);
		}
		if (shader.id) {
			sg_destroy_shader(shader);
		}
	}
};


struct RenderProgramSet
{

	Handle<Program> prog_tile;
	Handle<Program> prog_overlay;
	Handle<Program> prog_grid;
	Handle<Program> prog_mask;
	Handle<Program> prog_outline_dilate;
	Handle<Program> prog_outline_blend;
	Handle<Program> prog_ui;
};


struct Texture
{
	sg_image   image;
	sg_sampler sampler;

	int32_t width;
	int32_t height;

	Texture() = delete;
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(sg_image image_in, sg_sampler sampler_in, int width_in, int height_in) noexcept
		: image   {image_in}
		, sampler {sampler_in}
		, width   {width_in}
		, height  {height_in}
	{}

	Texture(Texture&& other) noexcept
	{
		image   = other.image;
		sampler = other.sampler;
		width   = other.width;
		height  = other.height;

		other.image   = {};
		other.sampler = {};
		other.width   = 0;
		other.height  = 0;
	}

	Texture& operator=(Texture&& other) noexcept
	{
		if (this != &other) {
			if (image.id) {
				sg_destroy_image(image);
			}
			if (sampler.id) {
				sg_destroy_sampler(sampler);
			}

			image   = other.image;
			sampler = other.sampler;
			width   = other.width;
			height  = other.height;

			other.image   = {};
			other.sampler = {};
			other.width   = 0;
			other.height  = 0;
		}
		return *this;
	}

	~Texture()
	{
		if (image.id) {
			sg_destroy_image(image);
		}
		if (sampler.id) {
			sg_destroy_sampler(sampler);
		}
	}
};


// TODO: this is identical to a Texture - remove

struct FontTexture
{
	sg_image   image;
	sg_sampler sampler;

	int width;
	int height;

	FontTexture() = delete;
	FontTexture(const FontTexture&) = delete;
	FontTexture& operator=(const FontTexture&) = delete;

	FontTexture(sg_image image_in, sg_sampler sampler_in, int width_in, int height_in) noexcept
		: image   {image_in}
		, sampler {sampler_in}
		, width   {width_in}
		, height  {height_in}
	{}

	FontTexture(FontTexture&& other) noexcept
	{
		image   = other.image;
		sampler = other.sampler;
		width   = other.width;
		height  = other.height;

		other.image   = {};
		other.sampler = {};
		other.width   = 0;
		other.height  = 0;
	}

	FontTexture& operator=(FontTexture&& other) noexcept
	{
		if (this != &other) {
			if (image.id) {
				sg_destroy_image(image);
			}
			if (sampler.id) {
				sg_destroy_sampler(sampler);
			}

			image   = other.image;
			sampler = other.sampler;
			width   = other.width;
			height  = other.height;

			other.image   = {};
			other.sampler = {};
			other.width   = 0;
			other.height  = 0;
		}
		return *this;
	}

	~FontTexture()
	{
		if (image.id) {
			sg_destroy_image(image);
		}
		if (sampler.id) {
			sg_destroy_sampler(sampler);
		}
	}
};


struct SubmeshGeometry
{
	mtp::vault<uint8_t, mtp::default_set> index_bytes;

	SubmeshGeometry() = delete;
	SubmeshGeometry(const SubmeshGeometry&) = delete;
	SubmeshGeometry& operator=(const SubmeshGeometry&) = delete;

	SubmeshGeometry(SubmeshGeometry&&) noexcept = default;

	explicit SubmeshGeometry(mtp::vault<uint8_t, mtp::default_set>&& index_bytes_in) noexcept
		: index_bytes {std::move(index_bytes_in)}
	{}
};


struct MeshGeometry
{
	mtp::vault<uint8_t,         mtp::default_set> vertex_bytes;
	mtp::vault<SubmeshGeometry, mtp::default_set> submeshes;

	MeshGeometry() = delete;
	MeshGeometry(const MeshGeometry&) = delete;
	MeshGeometry& operator=(const MeshGeometry&) = delete;

	MeshGeometry(MeshGeometry&&) noexcept = default;

	explicit MeshGeometry(
		mtp::vault<uint8_t, mtp::default_set>&& vertex_bytes_in,
		mtp::vault<uint8_t, mtp::default_set>&& index_bytes_in
	) noexcept
		: vertex_bytes {std::move(vertex_bytes_in)}
		, submeshes    {}
	{
		submeshes.emplace_back(std::move(index_bytes_in));
	}
};


// TODO: reassess mesh geometry / submesh geometry relation

struct Submesh
{
	uint32_t  first_idx;
	uint32_t  idx_count;
	sg_buffer idx_buffer;
};


struct Mesh
{
	Handle<MeshGeometry> geometry;

	uint32_t vtx_count;
	uint32_t idx_count;

	sg_bindings bindings;

	mtp::vault<Submesh, mtp::default_set> submeshes;

	Mesh() = default;

	Mesh(Handle<MeshGeometry> geometry_hnd, uint32_t vtx_count_in, uint32_t idx_count_in, const sg_bindings& bindings_in)
		: geometry  {geometry_hnd}
		, vtx_count {vtx_count_in}
		, idx_count {idx_count_in}
		, bindings  {bindings_in}
		, submeshes {}
	{}

	Mesh(const Mesh&)                = delete;
	Mesh& operator=(const Mesh&)     = delete;
	Mesh(Mesh&&) noexcept            = default;
	Mesh& operator=(Mesh&&) noexcept = default;
};


enum class AlphaMode : uint8_t
{
	Opaque = 0,
	Mask   = 1,
	Blend  = 2
};


// TODO: rule 6 to the bottom

struct MaterialTemplate
{
	// NOTE: all other programs flow through the RenderProgramSet - consider
	Handle<Program> program;

	std::array<Handle<Texture>, cfg::max_tex_per_mat> textures;
	std::array<uint8_t,         cfg::max_tex_per_mat> uv_index;

	uint32_t map_mask;

	AlphaMode alpha_mode {AlphaMode::Opaque};
};


struct MaterialInstance
{
	Handle<MaterialTemplate> mat_template;

	uint32_t map_mask;

	vec4 albedo_tint;

	float metallic_factor;
	float roughness_factor;
	float ao_strength;
	float normal_scale;

	vec3 emissive_factor;

	vec2 uv_scale;
	vec2 uv_offset;
};


} // hpr::rdr

