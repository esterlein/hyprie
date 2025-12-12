#pragma once

#include "sokol_gfx.h"

#include "render_hub.hpp"
#include "render_data.hpp"
#include "asset_bank.hpp"
#include "asset_data.hpp"
#include "forge_cache.hpp"

#include "scene_data.hpp"
#include "fx_data.hpp"
#include "editor_data.hpp"
#include "handle_resolver.hpp"


namespace hpr::rdr {


using ForgeResolver = res::HandleResolver <
	res::ResolverEntry<res::ImageResource,    const res::AssetBank<res::ImageResource>>,
	res::ResolverEntry<res::MaterialResource, const res::AssetBank<res::MaterialResource>>
>;


class RenderForge
{
public:

	RenderForge(RenderHub& hub, const ForgeResolver& resolver, SurfaceInfo surface_info);

	scn::ScenePrimitive create_scene_primitive(res::ImportPrimitive& import_primitive);

	RenderProgramSet get_render_programs() const;

	// TODO: reassess the shader-pipeline flow to the rendering subsystems
	Handle<Program> overlay_program() const
	{ return m_prog_overlay; }

	Handle<FontTexture> create_font_texture(const void* pixels_rgba32, int width, int height);
	void update_font_texture(
		Handle<FontTexture> font_texture,
		const void*         pixels_rgba32,
		int                 width,
		int                 height
	);

	Handle<Texture> create_tilemap_texture(
		int32_t width,
		int32_t height
	);

	void update_tilemap_texture(
		Handle<Texture>           tex_hnd,
		std::span<const uint16_t> tex_bytes,
		int32_t                   width,
		int32_t                   height
	);

	Handle<Mesh> create_overlay_mesh(
		const mtp::vault<vec3,     mtp::default_set>& positions,
		const mtp::vault<uint32_t, mtp::default_set>& indices,
		std::initializer_list<edt::GeometryRange>     submesh_ranges,
		uint64_t                                      cache_key
	);

	Handle<Mesh> create_dynamic_mesh(
		uint32_t vtx_stride,
		uint32_t vtx_capacity,
		uint32_t idx_capacity
	);

	void update_dynamic_mesh(
		Handle<Mesh>               mesh_hnd,
		std::span<const std::byte> vtx_span,
		uint32_t                   vtx_count,
		std::span<const std::byte> idx_span,
		uint32_t                   idx_count
	);

	Handle<TileStyle> create_tile_style();

	Handle<Mesh> quad() const
	{ return m_quad; }

private:

	void init_scene_pipeline();
	void init_tile_pipeline();
	void init_ui_pipeline();
	void init_overlay_pipeline();
	void init_grid_pipeline();
	void init_mask_pipeline();
	void init_dilate_pipeline();
	void init_blend_pipeline();

	void init_quad();

	Handle<Program> create_program(
		const sg_shader_desc*   shader_desc,
		const sg_pipeline_desc& pipeline_desc,
		ProgramFlagsMode        flags_mode
	);

	sg_image make_solid_rgba8(uint32_t rgba, bool is_srgb);
	sg_sampler make_linear_repeat();

	Handle<MeshGeometry> create_geometry(res::ImportPrimitiveGeometry& import_geometry);

	Handle<Mesh> create_mesh(
		Handle<MeshGeometry> geometry_handle,
		uint32_t             vtx_count,
		uint64_t             vtx_buf_key
	);

	uint32_t create_submesh(
		Handle<Mesh>         mesh_handle,
		Handle<MeshGeometry> geometry_handle,
		uint32_t             idx_count,
		uint64_t             idx_buf_key
	);

	Handle<MaterialTemplate> create_material_template(
		Handle<res::MaterialResource> template_resource,
		Handle<Program>               program
	);

	Handle<MaterialInstance> create_material_instance(
		Handle<res::MaterialResource> template_resource,
		Handle<MaterialTemplate>      material_template
	);

	Handle<Texture> create_texture(
		const void*            pixel_data,
		int                    width,
		int                    height,
		const sg_sampler_desc* sampler_desc_ptr,
		const char*            label,
		bool                   srgb
	);

private:

	RenderHub&  m_hub;
	ForgeCache  m_cache;
	SurfaceInfo m_surface_info;

	Handle<Program> m_prog_scene;
	Handle<Program> m_prog_tile;
	Handle<Program> m_prog_ui;
	Handle<Program> m_prog_overlay;
	Handle<Program> m_prog_grid;
	Handle<Program> m_prog_outline_mask;
	Handle<Program> m_prog_outline_dilate;
	Handle<Program> m_prog_outline_blend;

	Handle<TileStyle> m_tile_style;

	const ForgeResolver& m_resolver;

	// TODO: make a struct for canonical geometry
	Handle<Mesh> m_quad;

	Handle<MaterialTemplate> m_default_material_template;
	Handle<MaterialInstance> m_default_material_instance;
};

} // hpr::rdr

