#include "log.hpp"
#include "handle.hpp"
#include "engine.hpp"
#include "render_forge.hpp"

#include "render_data.hpp"
#include "tile_draw_data.hpp"

#include "ui.glsl.h"
#include "tile.glsl.h"
#include "grid.glsl.h"
#include "scene.glsl.h"
#include "overlay.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_blend.glsl.h"
#include "outline_dilate.glsl.h"


#include "math.hpp"


namespace hpr::rdr {


RenderForge::RenderForge(RenderHub& hub, const ForgeResolver& resolver, SurfaceInfo surface_info)
	: m_hub          {hub}
	, m_resolver     {resolver}
	, m_surface_info {std::move(surface_info)}
{
	init_scene_pipeline();
	init_ui_pipeline();
	init_tile_pipeline();
	init_overlay_pipeline();
	init_grid_pipeline();
	init_mask_pipeline();
	init_dilate_pipeline();
	init_blend_pipeline();

	init_quad();
}


void RenderForge::init_scene_pipeline()
{
	const sg_shader_desc* scene_shader_desc = scene_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.layout.buffers[0].stride = sizeof(SceneVertex);

	pipeline_desc.layout.attrs[ATTR_scene_main_position].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_position].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_scene_main_position].offset = offsetof(SceneVertex, pos);

	pipeline_desc.layout.attrs[ATTR_scene_main_normal].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_normal].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_scene_main_normal].offset = offsetof(SceneVertex, nrm);

	pipeline_desc.layout.attrs[ATTR_scene_main_tangent].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_tangent].format = SG_VERTEXFORMAT_FLOAT4;
	pipeline_desc.layout.attrs[ATTR_scene_main_tangent].offset = offsetof(SceneVertex, tan);

	pipeline_desc.layout.attrs[ATTR_scene_main_uv_0_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv_0_in].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv_0_in].offset = offsetof(SceneVertex, uv0);

	pipeline_desc.layout.attrs[ATTR_scene_main_uv_1_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv_1_in].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv_1_in].offset = offsetof(SceneVertex, uv1);

	pipeline_desc.layout.attrs[ATTR_scene_main_color].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_color].format = SG_VERTEXFORMAT_UBYTE4N;
	pipeline_desc.layout.attrs[ATTR_scene_main_color].offset = offsetof(SceneVertex, rgba);

	pipeline_desc.layout.attrs[ATTR_scene_main_extra].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_scene_main_extra].format = SG_VERTEXFORMAT_UINT;
	pipeline_desc.layout.attrs[ATTR_scene_main_extra].offset = offsetof(SceneVertex, ext);

	pipeline_desc.index_type          = SG_INDEXTYPE_UINT32;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline_desc.depth.write_enabled = true;
	pipeline_desc.cull_mode           = SG_CULLMODE_BACK;
	pipeline_desc.face_winding        = SG_FACEWINDING_CCW;
	pipeline_desc.sample_count        = m_surface_info.sample_count;

	m_prog_scene = create_program(scene_shader_desc, pipeline_desc, ProgramFlagsMode::pbr_maps);

	Handle<Texture> tex_hnd_albedo =
		m_hub.create<Texture>(make_solid_rgba8(0xFFFFFFFFU, true),  make_linear_repeat(), 1, 1);
	Handle<Texture> tex_hnd_normal =
		m_hub.create<Texture>(make_solid_rgba8(0x8080FFFFU, false), make_linear_repeat(), 1, 1);
	Handle<Texture> tex_hnd_orm =
		m_hub.create<Texture>(make_solid_rgba8(0xFFFF00FFU, false), make_linear_repeat(), 1, 1);
	Handle<Texture> tex_hnd_emissive =
		m_hub.create<Texture>(make_solid_rgba8(0x000000FFU, true),  make_linear_repeat(), 1, 1);

	MaterialTemplate default_material_template {};
	default_material_template.program = m_prog_scene;

	default_material_template.textures[Tex::alb] = tex_hnd_albedo;
	default_material_template.textures[Tex::nrm] = tex_hnd_normal;
	default_material_template.textures[Tex::orm] = tex_hnd_orm;
	default_material_template.textures[Tex::ems] = tex_hnd_emissive;

	for (auto texture_slot = 0; texture_slot < cfg::max_tex_per_mat; ++texture_slot) {
		default_material_template.uv_index[texture_slot] = 0;
	}

	default_material_template.map_mask = m_hub.get<Program>(m_prog_scene)->flags;

	m_default_material_template = m_hub.create<MaterialTemplate>(std::move(default_material_template));

	MaterialInstance default_material_instance {
		.mat_template     = m_default_material_template,
		.map_mask         = default_material_template.map_mask,
		.albedo_tint      = vec4(1.0f, 1.0f, 1.0f, 1.0f),
		.metallic_factor  = 0.0f,
		.roughness_factor = 1.0f,
		.ao_strength      = 1.0f,
		.normal_scale     = 1.0f,
		.emissive_factor  = vec3(0.0f, 0.0f, 0.0f),
		.uv_scale         = vec2(1.0f, 1.0f),
		.uv_offset        = vec2(0.0f, 0.0f)
	};

	m_default_material_instance = m_hub.create<MaterialInstance>(std::move(default_material_instance));
}


void RenderForge::init_tile_pipeline()
{
	const sg_shader_desc* shader_desc = tile_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.layout.buffers[0].stride = sizeof(QuadVertex);

	pipeline.layout.attrs[ATTR_tile_main_position_in].buffer_index = 0;
	pipeline.layout.attrs[ATTR_tile_main_position_in].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline.layout.attrs[ATTR_tile_main_position_in].offset = offsetof(QuadVertex, pos);

	pipeline.layout.attrs[ATTR_tile_main_uv_in].buffer_index = 0;
	pipeline.layout.attrs[ATTR_tile_main_uv_in].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline.layout.attrs[ATTR_tile_main_uv_in].offset = offsetof(QuadVertex, uv);

	pipeline.index_type          = SG_INDEXTYPE_UINT32;
	pipeline.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline.depth.write_enabled = true;
	pipeline.cull_mode           = SG_CULLMODE_NONE;
	pipeline.face_winding        = SG_FACEWINDING_CCW;

	pipeline.colors[0].blend.enabled          = true;
	pipeline.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline.sample_count           = m_surface_info.sample_count;

	m_prog_tile = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_ui_pipeline()
{
	const sg_shader_desc* shader_desc = ui_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.layout.buffers[0].stride = 20;

	pipeline.layout.attrs[ATTR_ui_main_position].buffer_index = 0;
	pipeline.layout.attrs[ATTR_ui_main_position].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline.layout.attrs[ATTR_ui_main_position].offset = 0;

	pipeline.layout.attrs[ATTR_ui_main_uv_in].buffer_index = 0;
	pipeline.layout.attrs[ATTR_ui_main_uv_in].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline.layout.attrs[ATTR_ui_main_uv_in].offset = 8;

	pipeline.layout.attrs[ATTR_ui_main_color_in].buffer_index = 0;
	pipeline.layout.attrs[ATTR_ui_main_color_in].format = SG_VERTEXFORMAT_UBYTE4N;
	pipeline.layout.attrs[ATTR_ui_main_color_in].offset = 16;

	pipeline.index_type          = SG_INDEXTYPE_UINT16;
	pipeline.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline.depth.write_enabled = false;
	pipeline.cull_mode           = SG_CULLMODE_NONE;

	pipeline.colors[0].blend.enabled          = true;
	pipeline.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline.sample_count           = m_surface_info.sample_count;

	m_prog_ui = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_overlay_pipeline()
{
	const sg_shader_desc* shader_desc = overlay_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.layout.buffers[0].stride = sizeof(OverlayVertex);

	pipeline.layout.attrs[ATTR_overlay_main_position].buffer_index = 0;
	pipeline.layout.attrs[ATTR_overlay_main_position].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline.layout.attrs[ATTR_overlay_main_position].offset = offsetof(OverlayVertex, pos);

	pipeline.index_type          = SG_INDEXTYPE_UINT32;
	pipeline.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline.depth.write_enabled = false;
	pipeline.cull_mode           = SG_CULLMODE_NONE;
	pipeline.face_winding        = SG_FACEWINDING_CCW;

	pipeline.colors[0].blend.enabled          = true;
	pipeline.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline.depth.pixel_format     = m_surface_info.depth_format;
	pipeline.sample_count           = m_surface_info.sample_count;

	m_prog_overlay = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_grid_pipeline()
{
	const sg_shader_desc* shader_desc = grid_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.index_type          = SG_INDEXTYPE_NONE;
	pipeline.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline.depth.write_enabled = false;
	pipeline.cull_mode           = SG_CULLMODE_NONE;

	pipeline.colors[0].blend.enabled          = true;
	pipeline.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline.sample_count           = m_surface_info.sample_count;

	m_prog_grid = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_mask_pipeline()
{
	const sg_shader_desc* shader_desc = outline_mask_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.layout.buffers[0].stride = static_cast<int>(sizeof(SceneVertex));

	pipeline.layout.attrs[ATTR_outline_mask_main_position].buffer_index = 0;
	pipeline.layout.attrs[ATTR_outline_mask_main_position].format       = SG_VERTEXFORMAT_FLOAT3;
	pipeline.layout.attrs[ATTR_outline_mask_main_position].offset       = static_cast<int>(offsetof(SceneVertex, pos));

	pipeline.index_type              = SG_INDEXTYPE_UINT32;
	pipeline.depth.compare           = SG_COMPAREFUNC_ALWAYS;
	pipeline.depth.write_enabled     = false;
	pipeline.cull_mode               = SG_CULLMODE_NONE;
	pipeline.colors[0].blend.enabled = false;
	pipeline.colors[0].pixel_format  = SG_PIXELFORMAT_R8;
	pipeline.depth.pixel_format      = SG_PIXELFORMAT_NONE;
	pipeline.sample_count            = 1;

	m_prog_outline_mask = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_dilate_pipeline()
{
	const sg_shader_desc* shader_desc = outline_dilate_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.index_type              = SG_INDEXTYPE_NONE;
	pipeline.depth.compare           = SG_COMPAREFUNC_ALWAYS;
	pipeline.depth.write_enabled     = false;
	pipeline.cull_mode               = SG_CULLMODE_NONE;
	pipeline.colors[0].blend.enabled = false;
	pipeline.colors[0].pixel_format  = SG_PIXELFORMAT_R8;
	pipeline.depth.pixel_format      = SG_PIXELFORMAT_NONE;
	pipeline.sample_count            = 1;

	m_prog_outline_dilate = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


void RenderForge::init_blend_pipeline()
{
	const sg_shader_desc* shader_desc = outline_blend_main_shader_desc(sg_query_backend());

	sg_pipeline_desc pipeline {};
	pipeline.index_type          = SG_INDEXTYPE_NONE;
	pipeline.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline.depth.write_enabled = false;
	pipeline.cull_mode           = SG_CULLMODE_NONE;

	pipeline.colors[0].blend.enabled          = true;
	pipeline.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline.sample_count           = m_surface_info.sample_count;

	m_prog_outline_blend = create_program(shader_desc, pipeline, ProgramFlagsMode::none);
}


Handle<Program> RenderForge::create_program(
	const sg_shader_desc*   shader_desc,
	const sg_pipeline_desc& pipeline_desc,
	ProgramFlagsMode        flags_mode
)
{
	sg_shader shader_handle = sg_make_shader(shader_desc);

	sg_pipeline_desc desc = pipeline_desc;
	desc.shader = shader_handle;

	sg_pipeline pipeline_handle = sg_make_pipeline(&desc);

	Program program {shader_handle, pipeline_handle};

	program.color_format = desc.colors[0].pixel_format;
	program.depth_format = desc.depth.pixel_format;
	program.sample_count = desc.sample_count;

	// TODO: move constexpr flag array to render data

	if (flags_mode == ProgramFlagsMode::pbr_maps) {

		constexpr uint8_t pbr_map_flags[cfg::max_tex_per_mat] = {
			MapFlag::albedo,
			MapFlag::normal,
			MapFlag::ormh,
			MapFlag::emissive
		};

		for (int slot_index = 0; slot_index < cfg::max_tex_per_mat; ++slot_index) {
			const auto& img_smp_pair = shader_desc->image_sampler_pairs[slot_index];

			program.image_slots[slot_index]   = static_cast<uint8_t>(img_smp_pair.image_slot);
			program.sampler_slots[slot_index] = static_cast<uint8_t>(img_smp_pair.sampler_slot);

			// NOTE: map bit flag per texture inside program currently not used
			// NOTE: material instance contains map mask used for binding instead

			if (img_smp_pair.image_slot >= 0 && img_smp_pair.sampler_slot >= 0) {
				program.flags |= static_cast<uint8_t>(pbr_map_flags[slot_index]);
			}
		}
	}

	return m_hub.create<Program>(std::move(program));
}


sg_image RenderForge::make_solid_rgba8(uint32_t rgba, bool is_srgb)
{
	sg_image_desc image {};
	image.width  = 1;
	image.height = 1;
	image.pixel_format = is_srgb ? SG_PIXELFORMAT_SRGB8A8 : SG_PIXELFORMAT_RGBA8;
	image.data.subimage[0][0].ptr = &rgba;
	image.data.subimage[0][0].size = sizeof(rgba);
	return sg_make_image(&image);
}


sg_sampler RenderForge::make_linear_repeat()
{
	sg_sampler_desc sampler {};
	sampler.min_filter = SG_FILTER_LINEAR;
	sampler.mag_filter = SG_FILTER_LINEAR;
	sampler.wrap_u     = SG_WRAP_REPEAT;
	sampler.wrap_v     = SG_WRAP_REPEAT;
	return sg_make_sampler(&sampler);
}


scn::ScenePrimitive RenderForge::create_scene_primitive(res::ImportPrimitive& import_primitive)
{
	Handle<MeshGeometry> geometry_handle = create_geometry(import_primitive.geometry);
	Handle<Mesh> mesh_handle = create_mesh(geometry_handle, import_primitive.geometry.vtx_count, import_primitive.geometry.vtx_buf_key);
	uint32_t submesh_index = create_submesh(mesh_handle, geometry_handle, import_primitive.geometry.idx_count, import_primitive.geometry.idx_buf_key);

	Handle<MaterialTemplate> material_template_handle = create_material_template(import_primitive.material_template, m_prog_scene);
	Handle<MaterialInstance> material_instance_handle = create_material_instance(import_primitive.material_template, material_template_handle);

	scn::ScenePrimitive scene_primitive {
		.mesh        = mesh_handle,
		.submesh_idx = submesh_index,
		.material    = material_instance_handle
	};

	return scene_primitive;
}


Handle<MeshGeometry> RenderForge::create_geometry(res::ImportPrimitiveGeometry& import_geometry)
{
	return m_hub.create<MeshGeometry>(std::move(import_geometry.vtx_bytes), std::move(import_geometry.idx_bytes));
}


Handle<Mesh> RenderForge::create_mesh(Handle<MeshGeometry> geometry_handle, uint32_t vtx_count, uint64_t vtx_buf_key)
{
	MeshGeometry* mesh_geometry = m_hub.get<MeshGeometry>(geometry_handle);

	sg_buffer vertex_buffer = m_cache.find_vtx_buffer(vtx_buf_key);
	if (!vertex_buffer.id) {
		sg_buffer_desc buffer_desc {};
		buffer_desc.usage.vertex_buffer = true;
		buffer_desc.usage.immutable     = true;
		buffer_desc.data.ptr            = mesh_geometry->vertex_bytes.data();
		buffer_desc.data.size           = mesh_geometry->vertex_bytes.size();
		buffer_desc.label               = "vtx_buf";
		vertex_buffer = sg_make_buffer(&buffer_desc);
		m_cache.put_vtx_buffer(vtx_buf_key, vertex_buffer);
	}

	MeshKey mesh_key {
		.vtx_buf_id = vertex_buffer.id,
		.idx_buf_id = 0,
		.vtx_count  = vtx_count,
		.idx_count  = 0
	};

	if (auto mesh_handle = m_cache.find_mesh(mesh_key); mesh_handle.is_valid())
		return mesh_handle;

	sg_bindings mesh_bindings {};
	mesh_bindings.vertex_buffers[0] = vertex_buffer;

	Handle<Mesh> mesh_handle = m_hub.create<Mesh>(geometry_handle, vtx_count, 0, mesh_bindings);
	m_cache.put_mesh(mesh_key, mesh_handle);
	return mesh_handle;
}


uint32_t RenderForge::create_submesh(Handle<Mesh> mesh_handle, Handle<MeshGeometry> geometry_handle, uint32_t idx_count, uint64_t idx_buf_key)
{
	Mesh* mesh = m_hub.get<Mesh>(mesh_handle);
	MeshGeometry* mesh_geometry = m_hub.get<MeshGeometry>(geometry_handle);

	sg_buffer index_buffer = m_cache.find_idx_buffer(idx_buf_key);
	if (!index_buffer.id) {
		const SubmeshGeometry& submesh_geometry = mesh_geometry->submeshes[0];

		sg_buffer_desc buffer_desc {};
		buffer_desc.usage.index_buffer = true;
		buffer_desc.usage.immutable    = true;
		buffer_desc.data.ptr           = submesh_geometry.index_bytes.data();
		buffer_desc.data.size          = submesh_geometry.index_bytes.size();
		buffer_desc.label              = "idx_buf";

		index_buffer = sg_make_buffer(&buffer_desc);

		m_cache.put_idx_buffer(idx_buf_key, index_buffer);
	}

	for (uint32_t i = 0; i < static_cast<uint32_t>(mesh->submeshes.size()); ++i) {
		const Submesh& submesh = mesh->submeshes[i];
		if (submesh.idx_buffer.id == index_buffer.id && submesh.idx_count == idx_count && submesh.first_idx == 0)
			return i;
	}

	Submesh submesh {
		.first_idx  = 0,
		.idx_count  = idx_count,
		.idx_buffer = index_buffer
	};

	const uint32_t submesh_index = static_cast<uint32_t>(mesh->submeshes.size());
	mesh->submeshes.emplace_back(submesh);
	mesh->idx_count += idx_count;

	return submesh_index;
}


Handle<MaterialTemplate> RenderForge::create_material_template(Handle<res::MaterialResource> template_res_hnd, Handle<Program> program_hnd)
{
	HPR_ASSERT(template_res_hnd.is_valid());
	HPR_ASSERT(program_hnd.is_valid());

	const res::MaterialResource* mat_template_res = m_resolver.resolve<res::MaterialResource>(template_res_hnd);

	MaterialTemplate material_template {};
	material_template.program = program_hnd;

	auto make_template_texture = [this](Handle<res::ImageResource> image_handle, bool srgb) -> Handle<Texture>
	{
		const res::ImageResource* image = m_resolver.resolve<res::ImageResource>(image_handle);

		if (!image) {
			HPR_WARN(
				log::LogCategory::render,
				"[forge][create_material_template] tex resolve fail [handle %u][srgb %d]",
				static_cast<unsigned>(image_handle.index),
				srgb ? 1 : 0
			);
			return Handle<Texture>::null();
		}

		TexKey tex_key {
			.source_key  = static_cast<uint64_t>(image_handle.index),
			.sampler_key = 0,
			.srgb        = srgb
		};

		if (auto cached = m_cache.find_texture(tex_key); cached.is_valid()) {
			return cached;
		}

		Handle<Texture> texture = create_texture(
			image->pixels.data(),
			static_cast<int>(image->width),
			static_cast<int>(image->height),
			nullptr,
			nullptr,
			srgb
		);

		if (texture.is_valid()) {
			m_cache.put_texture(tex_key, texture);
			HPR_DEBUG(
				log::LogCategory::render,
				"[forge][create_material_template] tex created [%ux%u][srgb %d][index %u]",
				static_cast<unsigned>(image->width),
				static_cast<unsigned>(image->height),
				srgb ? 1 : 0,
				static_cast<unsigned>(texture.index)
			);
		}
		else {
			HPR_ERROR(
				log::LogCategory::render,
				"[forge][create_material_template] create tex fail [handle %u][srgb %d]",
				static_cast<unsigned>(image_handle.index),
				srgb ? 1 : 0
			);
		}
		return texture;
	};

	const auto default_template = m_hub.get<MaterialTemplate>(m_default_material_template);

	Handle<Texture> tex_hnd_albedo {};
	if (mat_template_res && mat_template_res->has_albedo()) {
		tex_hnd_albedo = make_template_texture(mat_template_res->textures[res::Tex_Albedo], true);
	}
	if (!tex_hnd_albedo.is_valid()) {
		tex_hnd_albedo = default_template->textures[Tex::alb];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [albedo %u]",
			static_cast<unsigned>(tex_hnd_albedo.index)
		);
	}

	Handle<Texture> tex_hnd_normal {};
	if (mat_template_res && mat_template_res->has_normal()) {
		tex_hnd_normal = make_template_texture(mat_template_res->textures[res::Tex_Normal], false);
	}
	if (!tex_hnd_normal.is_valid()) {
		tex_hnd_normal = default_template->textures[Tex::nrm];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [normal %u]",
			static_cast<unsigned>(tex_hnd_normal.index)
		);
	}

	Handle<Texture> tex_hnd_orm {};
	if (mat_template_res && mat_template_res->has_ormh()) {
		tex_hnd_orm = make_template_texture(mat_template_res->textures[res::Tex_ORMH], false);
	}
	if (!tex_hnd_orm.is_valid()) {
		tex_hnd_orm = default_template->textures[Tex::orm];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [orm %u]",
			static_cast<unsigned>(tex_hnd_orm.index)
		);
	}

	Handle<Texture> tex_hnd_emissive {};
	if (mat_template_res && mat_template_res->has_emissive()) {
		tex_hnd_emissive = make_template_texture(mat_template_res->textures[res::Tex_Emissive], true);
	}
	if (!tex_hnd_emissive.is_valid()) {
		tex_hnd_emissive = default_template->textures[Tex::ems];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [emissive %u]",
			static_cast<unsigned>(tex_hnd_emissive.index)
		);
	}

	material_template.textures[Tex::alb] = tex_hnd_albedo;
	material_template.textures[Tex::nrm] = tex_hnd_normal;
	material_template.textures[Tex::orm] = tex_hnd_orm;
	material_template.textures[Tex::ems] = tex_hnd_emissive;

	for (int slot = 0; slot < cfg::max_tex_per_mat; ++slot) {
		material_template.uv_index[slot] = mat_template_res ? static_cast<uint8_t>(mat_template_res->uv_index[slot]) : 0;
	}

	const uint32_t program_mask  = m_hub.get<Program>(program_hnd)->flags;
	const uint32_t resource_mask = mat_template_res ? mat_template_res->map_mask : 0;
	material_template.map_mask   = program_mask & resource_mask;

	HPR_DEBUG(
		log::LogCategory::render,
		"[forge][create_material_template] mat tpl mask [prog %u][res %u][tpl %u]",
		program_mask,
		resource_mask,
		material_template.map_mask
	);

	return m_hub.create<MaterialTemplate>(std::move(material_template));
}


Handle<MaterialInstance> RenderForge::create_material_instance(Handle<res::MaterialResource> template_res_hnd, Handle<MaterialTemplate> mat_template_hnd)
{
	assert(template_res_hnd.is_valid());
	assert(mat_template_hnd.is_valid());

	MaterialInstance material_instance {};
	material_instance.mat_template = mat_template_hnd;
	material_instance.map_mask = m_hub.get<MaterialTemplate>(mat_template_hnd)->map_mask;

	const res::MaterialResource* mat_template_res = m_resolver.resolve<res::MaterialResource>(template_res_hnd);

	if (mat_template_res) {
		material_instance.albedo_tint      = mat_template_res->albedo_tint;
		material_instance.emissive_factor  = mat_template_res->emissive_factor;

		material_instance.metallic_factor  = mat_template_res->metallic_factor;
		material_instance.roughness_factor = mat_template_res->roughness_factor;
		material_instance.ao_strength      = mat_template_res->ao_strength;
		material_instance.normal_scale     = mat_template_res->normal_scale;
	}
	else {
		material_instance.albedo_tint      = vec4(1.0f, 1.0f, 1.0f, 1.0f);
		material_instance.emissive_factor  = vec3(0.0f, 0.0f, 0.0f);

		material_instance.metallic_factor  = 0.0f;
		material_instance.roughness_factor = 1.0f;
		material_instance.ao_strength      = 1.0f;
		material_instance.normal_scale     = 1.0f;
	}

	material_instance.uv_scale  = vec2(1.0f, 1.0f);
	material_instance.uv_offset = vec2(0.0f, 0.0f);

	return m_hub.create<MaterialInstance>(std::move(material_instance));
}


Handle<Texture> RenderForge::create_texture(
	const void*            pixel_data,
	int                    width,
	int                    height,
	const sg_sampler_desc* sampler_desc_ptr,
	const char*            label,
	bool                   srgb
)
{
	assert(pixel_data);
	assert(width  > 0);
	assert(height > 0);

	sg_image_desc image_desc {};
	image_desc.type            = SG_IMAGETYPE_2D;
	image_desc.width           = width;
	image_desc.height          = height;
	image_desc.pixel_format    = srgb ? SG_PIXELFORMAT_SRGB8A8 : SG_PIXELFORMAT_RGBA8;
	image_desc.num_mipmaps     = 1;
	image_desc.usage.immutable = true;
	image_desc.label           = label ? label : "img";

	image_desc.data.subimage[0][0].ptr  = pixel_data;
	image_desc.data.subimage[0][0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

	sg_image image = sg_make_image(&image_desc);

	sg_sampler sampler {};
	if (sampler_desc_ptr) {
		sampler = sg_make_sampler(sampler_desc_ptr);
	}
	else {
		sampler = make_linear_repeat();
	}

	return m_hub.create<Texture>(image, sampler, width, height);
}


Handle<FontTexture> RenderForge::create_font_texture(const void* pixels_rgba32, int width, int height)
{
	assert(pixels_rgba32);
	assert(width  > 0);
	assert(height > 0);

	sg_image_desc image_desc {};
	image_desc.type            = SG_IMAGETYPE_2D;
	image_desc.width           = width;
	image_desc.height          = height;
	image_desc.pixel_format    = SG_PIXELFORMAT_RGBA8;
	image_desc.num_mipmaps     = 1;
	image_desc.usage.immutable = false;
	image_desc.label           = "ui_font_atlas";

	image_desc.data.subimage[0][0].ptr  = pixels_rgba32;
	image_desc.data.subimage[0][0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

	sg_image image_handle = sg_make_image(&image_desc);

	sg_sampler_desc sampler_desc {};
	sampler_desc.min_filter = SG_FILTER_LINEAR;
	sampler_desc.mag_filter = SG_FILTER_LINEAR;
	sampler_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	sampler_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;

	sg_sampler sampler_handle = sg_make_sampler(&sampler_desc);

	return m_hub.create<FontTexture>(image_handle, sampler_handle, width, height);
}


void RenderForge::update_font_texture(Handle<FontTexture> font_texture, const void* pixels_rgba32, int width, int height)
{
	assert(pixels_rgba32);
	assert(width  > 0);
	assert(height > 0);

	FontTexture* current = m_hub.get<FontTexture>(font_texture);
	if (!current)
		return;

	if (current->image.id && current->width == width && current->height == height) {

		sg_image_data image_data {};
		image_data.subimage[0][0].ptr  = pixels_rgba32;
		image_data.subimage[0][0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

		sg_update_image(current->image, &image_data);
		return;
	}

	if (current->image.id) {
		sg_destroy_image(current->image);
		current->image = {};
	}

	sg_image_desc image_desc {};
	image_desc.type            = SG_IMAGETYPE_2D;
	image_desc.width           = width;
	image_desc.height          = height;
	image_desc.pixel_format    = SG_PIXELFORMAT_RGBA8;
	image_desc.num_mipmaps     = 1;
	image_desc.usage.immutable = false;
	image_desc.label           = "ui_font_atlas";

	image_desc.data.subimage[0][0].ptr  = pixels_rgba32;
	image_desc.data.subimage[0][0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

	current->image  = sg_make_image(&image_desc);
	current->width  = width;
	current->height = height;

	if (!current->sampler.id) {
		sg_sampler_desc sampler_desc {};
		sampler_desc.min_filter = SG_FILTER_LINEAR;
		sampler_desc.mag_filter = SG_FILTER_LINEAR;
		sampler_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
		sampler_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;

		current->sampler = sg_make_sampler(&sampler_desc);
	}
}


Handle<Mesh> RenderForge::create_overlay_mesh(
	const mtp::vault<vec3, mtp::default_set>&     positions,
	const mtp::vault<uint32_t, mtp::default_set>& indices,
	std::initializer_list<edt::GeometryRange>     submesh_ranges,
	uint64_t                                      cache_key
)
{
	mtp::vault<uint8_t, mtp::default_set> vtx_bytes;
	mtp::vault<uint8_t, mtp::default_set> idx_bytes;

	const size_t vtx_byte_count = static_cast<size_t>(positions.size()) * sizeof(vec3);
	const size_t idx_byte_count = static_cast<size_t>(indices.size())   * sizeof(uint32_t);

	vtx_bytes.resize(vtx_byte_count);
	idx_bytes.resize(idx_byte_count);

	std::memcpy(vtx_bytes.data(), positions.data(), vtx_byte_count);
	std::memcpy(idx_bytes.data(),   indices.data(), idx_byte_count);

	Handle<MeshGeometry> geometry_handle = m_hub.create<MeshGeometry>(std::move(vtx_bytes), std::move(idx_bytes));

	const uint32_t vertex_count = static_cast<uint32_t>(positions.size());
	Handle<Mesh> mesh_handle    = create_mesh(geometry_handle, vertex_count, cache_key);

	Mesh* mesh = m_hub.get<Mesh>(mesh_handle);
	MeshGeometry* geometry = m_hub.get<MeshGeometry>(geometry_handle);

	sg_buffer idx_buffer = m_cache.find_idx_buffer(cache_key);
	if (!idx_buffer.id) {
		const SubmeshGeometry& submesh_geometry = geometry->submeshes[0];

		sg_buffer_desc idx_buffer_desc {};
		idx_buffer_desc.usage.index_buffer = true;
		idx_buffer_desc.usage.immutable    = true;
		idx_buffer_desc.data.ptr           = submesh_geometry.index_bytes.data();
		idx_buffer_desc.data.size          = submesh_geometry.index_bytes.size();
		idx_buffer_desc.label              = "idx_buf";

		idx_buffer = sg_make_buffer(&idx_buffer_desc);
		m_cache.put_idx_buffer(cache_key, idx_buffer);
	}

	uint64_t total_idx_count = 0;
	for (const edt::GeometryRange& range : submesh_ranges) {
		Submesh submesh {
			.first_idx  = range.first_idx,
			.idx_count  = range.idx_count,
			.idx_buffer = idx_buffer
		};
		mesh->submeshes.emplace_back(submesh);
		total_idx_count += static_cast<uint64_t>(range.idx_count);
	}
	mesh->idx_count = static_cast<uint32_t>(total_idx_count);

	return mesh_handle;
}


Handle<Mesh> RenderForge::create_dynamic_mesh(
	uint32_t vtx_stride,
	uint32_t vtx_capacity,
	uint32_t idx_capacity
)
{
	HPR_ASSERT_MSG(vtx_stride   > 0, "vtx stride is zero");
	HPR_ASSERT_MSG(vtx_capacity > 0, "vtx capacity is zero");
	HPR_ASSERT_MSG(idx_capacity > 0, "idx capacity is zero");

	const uint32_t vtx_capacity_bytes = vtx_stride * vtx_capacity;
	const uint32_t idx_capacity_bytes = static_cast<uint32_t>(sizeof(uint32_t)) * idx_capacity;

	sg_buffer_desc vtx_desc {};
	vtx_desc.size                 = static_cast<int>(vtx_capacity_bytes);
	vtx_desc.usage.vertex_buffer  = true;
	vtx_desc.usage.dynamic_update = true;
	vtx_desc.label                = "vtx_dyn_buf";

	sg_buffer_desc idx_desc {};
	idx_desc.size                 = static_cast<int>(idx_capacity_bytes);
	idx_desc.usage.index_buffer   = true;
	idx_desc.usage.dynamic_update = true;
	idx_desc.label                = "idx_dyn_buf";

	sg_buffer vtx_buffer = sg_make_buffer(&vtx_desc);
	sg_buffer idx_buffer = sg_make_buffer(&idx_desc);

	sg_bindings bindings {};
	bindings.vertex_buffers[0] = vtx_buffer;
	bindings.index_buffer      = idx_buffer;

	mtp::vault<uint8_t, mtp::default_set> vtx_bytes;
	vtx_bytes.reserve(static_cast<size_t>(vtx_capacity_bytes));

	mtp::vault<uint8_t, mtp::default_set> idx_bytes;
	idx_bytes.reserve(static_cast<size_t>(idx_capacity_bytes));

	MeshGeometry geometry {
		std::move(vtx_bytes),
		std::move(idx_bytes)
	};

	Handle<MeshGeometry> geometry_hnd = m_hub.create<MeshGeometry>(std::move(geometry));

	Mesh mesh {
		geometry_hnd,
		0,
		0,
		bindings
	};

	mesh.submeshes.emplace_back(Submesh {
		.first_idx  = 0,
		.idx_count  = 0,
		.idx_buffer = idx_buffer
	});

	return m_hub.create<Mesh>(std::move(mesh));
}


void RenderForge::update_dynamic_mesh(
	Handle<Mesh>               mesh_hnd,
	std::span<const std::byte> vtx_span,
	uint32_t                   vtx_count,
	std::span<const std::byte> idx_span,
	uint32_t                   idx_count
)
{
	Mesh* mesh = m_hub.get<Mesh>(mesh_hnd);

	// TODO: check for mesh validity

	HPR_ASSERT_MSG((vtx_count == 0) == vtx_span.empty(),
		"[forge] vtx_count / bytes mismatch");
	HPR_ASSERT_MSG((idx_count == 0) == idx_span.empty(),
		"[forge] idx_count / bytes mismatch");

	HPR_ASSERT_MSG(idx_span.size() == static_cast<size_t>(idx_count) * sizeof(uint32_t),
		"[forge] idx_bytes size mismatch");

	sg_buffer vtx_buf = mesh->bindings.vertex_buffers[0];
	sg_buffer idx_buf = mesh->bindings.index_buffer;

	HPR_ASSERT_MSG(vtx_buf.id != 0, "[forge] vtx buffer is invalid");
	HPR_ASSERT_MSG(idx_buf.id != 0, "[forge] idx buffer is invalid");

	if (!vtx_span.empty()) {
		const sg_range vtx_range {
			.ptr  = vtx_span.data(),
			.size = vtx_span.size()
		};
		sg_update_buffer(vtx_buf, &vtx_range);
	}

	if (!idx_span.empty()) {
		const sg_range idx_range {
			.ptr  = idx_span.data(),
			.size = idx_span.size()
		};
		sg_update_buffer(idx_buf, &idx_range);
	}

	mesh->vtx_count = vtx_count;
	mesh->idx_count = idx_count;

	HPR_ASSERT_MSG(!mesh->submeshes.empty(), "[forge] mesh has no submeshes");

	Submesh& submesh = mesh->submeshes[0];
	submesh.first_idx = 0;
	submesh.idx_count = idx_count;

	if (submesh.idx_buffer.id == 0) {
		submesh.idx_buffer = idx_buf;
	}
}


RenderProgramSet RenderForge::get_render_programs() const
{
	return RenderProgramSet {
		.prog_tile           = m_prog_tile,
		.prog_overlay        = m_prog_overlay,
		.prog_grid           = m_prog_grid,
		.prog_mask           = m_prog_outline_mask,
		.prog_outline_dilate = m_prog_outline_dilate,
		.prog_outline_blend  = m_prog_outline_blend,
		.prog_ui             = m_prog_ui
	};
}


void RenderForge::init_quad()
{
	// TODO: no magic numbers

	static constexpr rdr::QuadVertex vtx_values[4] {
		{vec3 {0.0f, 0.0f, 0.0f}, vec2 {0.0f, 0.0f}},
		{vec3 {1.0f, 0.0f, 0.0f}, vec2 {1.0f, 0.0f}},
		{vec3 {1.0f, 0.0f, 1.0f}, vec2 {1.0f, 1.0f}},
		{vec3 {0.0f, 0.0f, 1.0f}, vec2 {0.0f, 1.0f}}
	};

	static constexpr uint32_t idx_values[6] {
		0, 1, 2,
		0, 2, 3
	};

	mtp::vault<uint8_t, mtp::default_set> vtx_bytes;
	vtx_bytes.resize(sizeof(vtx_values));
	std::memcpy(vtx_bytes.data(), vtx_values, sizeof(vtx_values));

	mtp::vault<uint8_t, mtp::default_set> idx_bytes;
	idx_bytes.resize(sizeof(idx_values));
	std::memcpy(idx_bytes.data(), idx_values, sizeof(idx_values));

	Handle<MeshGeometry> geometry = m_hub.create<MeshGeometry>(
		std::move(vtx_bytes),
		std::move(idx_bytes)
	);

	sg_buffer_desc vtx_desc {};
	vtx_desc.data  = SG_RANGE(vtx_values);
	vtx_desc.size  = sizeof(vtx_values);
	vtx_desc.label = "quad_vtx";
	vtx_desc.usage.vertex_buffer = true;

	sg_buffer_desc idx_desc {};
	idx_desc.data  = SG_RANGE(idx_values);
	idx_desc.size  = sizeof(idx_values);
	idx_desc.label = "quad_idx";
	idx_desc.usage.index_buffer = true;

	sg_bindings bindings {};
	bindings.vertex_buffers[0] = sg_make_buffer(&vtx_desc);
	bindings.index_buffer      = sg_make_buffer(&idx_desc);

	Mesh mesh(geometry, 4, 6, bindings);

	mesh.submeshes.emplace_back(Submesh {
		.first_idx = 0,
		.idx_count = 6,
		.idx_buffer = bindings.index_buffer
	});

	m_quad = m_hub.create<Mesh>(std::move(mesh));
}


Handle<Texture> RenderForge::create_tilemap_texture(
	int32_t width,
	int32_t height
)
{
	HPR_ASSERT_MSG(width  > 0, "[forge] tilemap width <= 0");
	HPR_ASSERT_MSG(height > 0, "[forge] tilemap height <= 0");

	sg_sampler_desc sampler_desc {};
	sampler_desc.min_filter = SG_FILTER_NEAREST;
	sampler_desc.mag_filter = SG_FILTER_NEAREST;
	sampler_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	sampler_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;

	sg_image_desc image_desc {};
	image_desc.type                 = SG_IMAGETYPE_2D;
	image_desc.width                = width;
	image_desc.height               = height;
	image_desc.num_mipmaps          = 1;
	image_desc.pixel_format         = SG_PIXELFORMAT_R16UI;
	image_desc.usage.dynamic_update = true;
	image_desc.label                = "tex_tilemap";

	sg_image   image   = sg_make_image(&image_desc);
	sg_sampler sampler = sg_make_sampler(&sampler_desc);

	Texture texture {image, sampler, width, height};

	return m_hub.create<rdr::Texture>(std::move(texture));
}


void RenderForge::update_tilemap_texture(
	Handle<rdr::Texture>      tilemap_hnd,
	std::span<const uint16_t> tilemap_bytes,
	int32_t                   width,
	int32_t                   height
)
{
	Texture* texture = m_hub.get<rdr::Texture>(tilemap_hnd);
	if (!texture)
		return;

	HPR_ASSERT_MSG(width  > 0, "[forge] tilemap width <= 0");
	HPR_ASSERT_MSG(height > 0, "[forge] tilemap height <= 0");
	HPR_ASSERT_MSG(width == texture->width && height == texture->height,
		"[forge] tilemap size mismatch"
	);

	const size_t area_size = static_cast<size_t>(width) * static_cast<size_t>(height);
	HPR_ASSERT_MSG(tilemap_bytes.size() == area_size,
		"[forge] tilemap data size mismatch"
	);

	sg_image_data data {};
	data.subimage[0][0].ptr  = tilemap_bytes.data();
	data.subimage[0][0].size = tilemap_bytes.size_bytes();

	sg_update_image(texture->image, &data);
}


Handle<TileStyle> RenderForge::create_tile_style()
{
	if (m_tile_style.is_valid())
		return m_tile_style;

	HPR_ASSERT_MSG(m_prog_tile.is_valid(), "[forge] cue pipeline not initialized");

	std::array<uint32_t, 256> palette {};
	palette[0] = 0xFF00FFFFU;
	palette[1] = 0xFF00FFFFU;
	palette[2] = 0xFF00FFFFU;

	sg_sampler_desc sampler_desc {};
	sampler_desc.min_filter = SG_FILTER_NEAREST;
	sampler_desc.mag_filter = SG_FILTER_NEAREST;
	sampler_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	sampler_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;

	Handle<Texture> palette_tex = create_texture(
		palette.data(),
		256,
		1,
		&sampler_desc,
		"tile_cue_palette",
		true
	);

	TileStyle style {
		.palette      = palette_tex,
		.tile_params  = vec4(0.0f, 0.06f, 1.0f, 0.0f),
		.border_color = vec4(1.0f, 0.0f,  1.0f, 1.0f)
	};

	m_tile_style = m_hub.create<TileStyle>(std::move(style));
	return m_tile_style;
}


} // hpr::rdr

