#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "event.hpp"
#include "surface.hpp"
#include "render_hub.hpp"
#include "render_data.hpp"
#include "asset_bank.hpp"
#include "asset_data.hpp"
#include "render_cache.hpp"
#include "texture_data.hpp"
#include "texture_mass.hpp"
#include "tile_mass.hpp"
#include "font_mass.hpp"
#include "palette_mass.hpp"
#include "vertex_mass.hpp"
#include "storage_mass.hpp"

#include "scene.hpp"
#include "fx_data.hpp"
#include "font_data.hpp"
#include "scene_data.hpp"
#include "editor_data.hpp"
#include "pixel_format.hpp"
#include "vertex_utils.hpp"
#include "vertex_format.hpp"
#include "geometry_data.hpp"
#include "render_context.hpp"
#include "handle_resolver.hpp"
#include "components_render.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


using ForgeResolver = res::HandleResolver <
	res::ResolverEntry<res::ImageResource,    const res::AssetBank<res::ImageResource>>,
	res::ResolverEntry<res::MaterialResource, const res::AssetBank<res::MaterialResource>>
>;


class RenderForge
{
public:

	RenderForge(
		RenderHub&           hub,
		const ForgeResolver& resolver,
		SurfaceInfo          surface_info
	);

	~RenderForge();

	enum class MassDomain
	{
		assembler,
		storage
	};

	bool on_event(Event& event);

	Model create_model(const res::ImportModel& import_model);

	void emit_primitives(
		ecs::Entity             entity,
		const res::ImportModel& import_model,
		const Model&            model,
		scn::SceneRenderRig&    render_rig
	);

	Handle<Font> create_bitmap_font(
		const FontSpec&            spec,
		Handle<res::ImageResource> atlas_image,
		const FontMetrics&         metrics
	);

	Handle<Texture> create_texture(
		const void* pixel_data,
		uint32_t    width,
		uint32_t    height,
		PixelFormat pix_format
	);

	Handle<Texture> create_tilemap(
		uint32_t width,
		uint32_t height
	);

	Handle<Texture> create_palette();

	void update_tilemap(
		Handle<Texture>           tex_hnd,
		std::span<const uint16_t> tex_bytes,
		int32_t                   width,
		int32_t                   height
	);

	void update_nuklear_atlas(const void* pixels, int width, int height);

	BindingContext binding_context();
	StagingContext staging_context();

	void gpu_sync()
	{
		m_texture_mass.sync();

		m_scene_mass.sync();
		m_generic_mass.sync();
		m_bitmap_mass.sync();

		m_vtx_ssbo_mass.sync();
		m_idx_ssbo_mass.sync();

		m_scene_trs_mass.sync();
		m_mat_inst_mass.sync();
	}

private:

	void init_pipeline_scene();
	void init_pipeline_cue_solid();
	void init_pipeline_cue_wire();
	void init_pipeline_bitmap();
	void init_pipeline_overlay_solid();
	void init_pipeline_overlay_wire();
	void init_pipeline_grid();
	void init_pipeline_mask();
	void init_pipeline_dilate();
	void init_pipeline_blend();

	void create_samplers();
	void create_targets();

	template<typename Vertex>
	requires (
		std::is_same_v<Vertex, SceneVertex>   ||
		std::is_same_v<Vertex, GenericVertex> ||
		std::is_same_v<Vertex, BitmapVertex>
	)
	Handle<Mesh> create_mesh(
		const mtp::vault<Vertex,             mtp::default_set>& vertices,
		const mtp::vault<uint32_t,           mtp::default_set>& indices,
		const mtp::vault<res::ImportSubmesh, mtp::default_set>& submeshes
	)
	{
		auto& mass = get_vertex_mass<Vertex>();

		auto alloc = mass.push(
			vertices.data(), static_cast<uint32_t>(vertices.size()),
			indices.data(),  static_cast<uint32_t>(indices.size())
		);

		Mesh mesh {};
		mesh.vtx_base  = alloc.vtx_base;
		mesh.idx_first = alloc.idx_first;
		mesh.vtx_count = static_cast<uint32_t>(vertices.size());
		mesh.idx_count = static_cast<uint32_t>(indices.size());

		for (const auto& submesh : submeshes) {
			mesh.submeshes.push_back({
				.idx_first = submesh.idx_first + alloc.idx_first,
				.idx_count = submesh.idx_count,
				.vtx_base  = submesh.vtx_base  + alloc.vtx_base
			});
		}

		return m_hub.create<Mesh>(std::move(mesh));
	}

public:

	template<typename Vertex>
	Handle<Mesh> create_procedural_mesh(
		const mtp::vault<vec3,     mtp::default_set>& positions,
		const mtp::vault<uint32_t, mtp::default_set>& indices,
		const mtp::vault<vec2,     mtp::default_set>& uvs,
		std::span<const geo::Geoslice>           submesh_ranges
	)
	{
		const uint32_t vtx_count = static_cast<uint32_t>(positions.size());

		if constexpr (std::is_same_v<Vertex, SceneVertex>) {
			mtp::vault<SceneVertex, mtp::default_set> packed_vertices;
			packed_vertices.resize(vtx_count);

			for (uint32_t i = 0; i < vtx_count; ++i) {
				SceneVertex& vtx = packed_vertices[i];
				vtx.pos = {positions[i].x, positions[i].y, positions[i].z};
				vtx.nrm = pack_1010102(vec4(0.0f, 1.0f, 0.0f, 1.0f));
				vtx.tan = pack_1010102(vec4(1.0f, 0.0f, 0.0f, 1.0f));
				vtx.uv0 = {0, 0};
				vtx.uv1 = {0, 0};
				vtx.rgb = 0xFFFFFFFF;
			}

			mtp::vault<res::ImportSubmesh, mtp::default_set> import_submeshes;
			for (const auto& range : submesh_ranges) {
				import_submeshes.push_back(res::ImportSubmesh {
					.vtx_base  = range.vtx_base,
					.idx_first = range.idx_first,
					.idx_count = range.idx_count,
					.model_mat_idx = 0
				});
			}

			return create_mesh<Vertex>(packed_vertices, indices, import_submeshes);
		}
		else if constexpr (std::is_same_v<Vertex, GenericVertex>) {
			mtp::vault<GenericVertex, mtp::default_set> packed_vertices;
			packed_vertices.resize(vtx_count);

			for (uint32_t i = 0; i < vtx_count; ++i) {
				GenericVertex& vtx = packed_vertices[i];
				vtx.pos = {positions[i].x, positions[i].y, positions[i].z};
			
				vtx.uv.u = static_cast<uint16_t>(uvs[i].x * 65535.0f);
				vtx.uv.v = static_cast<uint16_t>(uvs[i].y * 65535.0f);
			}

			mtp::vault<res::ImportSubmesh, mtp::default_set> import_submeshes;
			for (const auto& range : submesh_ranges) {
				import_submeshes.push_back(res::ImportSubmesh {
					.vtx_base  = range.vtx_base,
					.idx_first = range.idx_first,
					.idx_count = range.idx_count,
					.model_mat_idx = 0
				});
			}

			return create_mesh<Vertex>(packed_vertices, indices, import_submeshes);
		}
	}

	template<MassDomain Domain = MassDomain::assembler, typename Vertex, typename Index>
	requires (
		std::is_same_v<Vertex, SceneVertex>   ||
		std::is_same_v<Vertex, GenericVertex> ||
		std::is_same_v<Vertex, BitmapVertex>
	)
	Submesh push_submesh(
		const mtp::vault<Vertex, mtp::default_set>& vertices,
		const mtp::vault<Index,  mtp::default_set>& indices
	)
	{
		if constexpr (Domain == MassDomain::assembler) {
			auto& mass = get_vertex_mass<Vertex>();

			auto alloc = mass.push(
				vertices.data(), static_cast<uint32_t>(vertices.size()),
				indices.data(),  static_cast<uint32_t>(indices.size())
			);

			return Submesh {
				.vtx_base  = alloc.vtx_base,
				.idx_first = alloc.idx_first,
				.idx_count = static_cast<uint32_t>(indices.size())
			};
		}
		else if constexpr (Domain == MassDomain::storage) {
			auto& vtx_mass = get_storage_mass<Vertex>();
			auto& idx_mass = get_storage_mass<Index>();

			return Submesh {
				.vtx_base  = vtx_mass.stage(vertices),
				.idx_first = idx_mass.stage(indices),
				.idx_count = static_cast<uint32_t>(indices.size())
			};
		}
	}

private:

	Handle<MaterialTemplate> create_material_template(
		Handle<res::MaterialResource> template_resource
	);

	Handle<MaterialInstance> create_material_instance(
		Handle<res::MaterialResource> template_resource,
		Handle<MaterialTemplate>      material_template
	);

	template <typename Vertex>
	auto& get_vertex_mass()
	{
		if constexpr (std::is_same_v<Vertex, SceneVertex>) {
			return m_scene_mass;
		} else if constexpr (std::is_same_v<Vertex, GenericVertex>) {
			return m_generic_mass;
		} else if constexpr (std::is_same_v<Vertex, BitmapVertex>) {
			return m_bitmap_mass;
		}
	}

	template <typename T>
	auto& get_storage_mass()
	{
		if constexpr (std::is_same_v<T, GenericVertex>) {
			return m_vtx_ssbo_mass;
		} else if constexpr (std::is_same_v<T, uint32_t>) {
			return m_idx_ssbo_mass;
		}
	}

private:

	RenderHub&  m_hub;
	RenderCache m_cache;
	SurfaceInfo m_surface_info;

	const ForgeResolver& m_resolver;

	Handle<MaterialTemplate> m_default_material_template;
	Handle<MaterialInstance> m_default_material_instance;

	TextureMass m_texture_mass;
	PaletteMass m_palette_mass;
	TileMass    m_tilemap_mass {cfg::num_tex_arrays};
	FontMass    m_font_mass    {cfg::num_tex_arrays};

	SamplerBind      m_sampler_bind;
	PipelineSet      m_pipelines;
	TargetFramebuffs m_targets;
	NuklearAtlasBind m_nk_atlas;

	VertexMass<SceneVertex,   uint32_t> m_scene_mass;
	VertexMass<GenericVertex, uint32_t> m_generic_mass;
	VertexMass<BitmapVertex,  uint16_t> m_bitmap_mass;

	StorageMass<SceneBlob>     m_scene_trs_mass;
	StorageMass<CueBlob>       m_cue_trs_mass;
	StorageMass<GenericVertex> m_vtx_ssbo_mass;
	StorageMass<uint32_t>      m_idx_ssbo_mass;
	StorageMass<OverlayBlob>   m_overlay_trs_mass;
	StorageMass<MaterialBlob>  m_mat_inst_mass;
};

} // hpr::rdr

