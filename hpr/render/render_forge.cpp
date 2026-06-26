#include "hprint.hpp"

#include "render_forge.hpp"

#include "mtp_memory.hpp"

#include "log.hpp"
#include "math.hpp"
#include "handle.hpp"
#include "entity.hpp"

#include "renderer.hpp"
#include "pixel_format.hpp"
#include "vertex_format.hpp"
#include "render_context.hpp"
#include "geometry_utils.hpp"
#include "event_dispatcher.hpp"

#include "tile_data.hpp"
#include "asset_data.hpp"
#include "render_data.hpp"
#include "texture_data.hpp"
#include "draw_tile_data.hpp"

#include "cue.glsl.h"
#include "cue_wire.glsl.h"
#include "grid.glsl.h"
#include "scene.glsl.h"
#include "bitmap.glsl.h"
#include "overlay.glsl.h"
#include "overlay_wire.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_blend.glsl.h"
#include "outline_dilate.glsl.h"

#include <cstddef>


namespace hpr::rdr {


RenderForge::RenderForge(
	RenderHub&           hub,
	const ForgeResolver& resolver,
	SurfaceInfo          surface_info
)
	: m_hub              {hub}
	, m_surface_info     {std::move(surface_info)}
	, m_resolver         {resolver}
	, m_scene_mass       {40'000'000U, 80'000'000U}
	, m_generic_mass     {    65'536U,    262'144U}
	, m_bitmap_mass      {    65'536U,    262'144U}
	, m_scene_trs_mass   {   500'000U}
	, m_cue_trs_mass     {     1'000U}
	, m_vtx_ssbo_mass     {   262'144U}
	, m_idx_ssbo_mass     {   262'144U}
	, m_overlay_trs_mass {     1'000U}
	, m_mat_inst_mass    {     1'000U}
{
	init_pipeline_scene();
	init_pipeline_bitmap();
	init_pipeline_cue_solid();
	init_pipeline_cue_wire();
	init_pipeline_overlay_solid();
	init_pipeline_overlay_wire();
	init_pipeline_grid();
	init_pipeline_mask();
	init_pipeline_dilate();
	init_pipeline_blend();

	create_palette();

	create_samplers();

	create_targets();

	uint32_t pix_alb_white   = 0xFFFFFFFFU;
	uint32_t pix_nrm_flat    = 0x8080FFFFU;
	uint32_t pix_orm_default = 0xFFFF00FFU;
	uint32_t pix_ems_off     = 0x000000FFU;

	Handle<Texture> tex_hnd_alb =
		create_texture(&pix_alb_white,   1, 1, PixelFormat::rgba8_srgb);
	Handle<Texture> tex_hnd_nrm =
		create_texture(&pix_nrm_flat,    1, 1, PixelFormat::rgba8_unorm);
	Handle<Texture> tex_hnd_orm =
		create_texture(&pix_orm_default, 1, 1, PixelFormat::rgba8_unorm);
	Handle<Texture> tex_hnd_ems =
		create_texture(&pix_ems_off,     1, 1, PixelFormat::rgba8_srgb);

	MaterialTemplate default_material_template {};
	default_material_template.textures[static_cast<size_t>(MatMapSlot::alb)] = tex_hnd_alb;
	default_material_template.textures[static_cast<size_t>(MatMapSlot::nrm)] = tex_hnd_nrm;
	default_material_template.textures[static_cast<size_t>(MatMapSlot::orm)] = tex_hnd_orm;
	default_material_template.textures[static_cast<size_t>(MatMapSlot::ems)] = tex_hnd_ems;

	for (auto texture_slot = 0; texture_slot < cfg::max_tex_per_mat; ++texture_slot) {
		default_material_template.uv_index[texture_slot] = 0;
	}

	default_material_template.map_mask =
		static_cast<uint8_t>(MatMapFlag::alb) |
		static_cast<uint8_t>(MatMapFlag::nrm) |
		static_cast<uint8_t>(MatMapFlag::orm) |
		static_cast<uint8_t>(MatMapFlag::ems);

	m_default_material_template =
		m_hub.create<MaterialTemplate>(std::move(default_material_template));

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

	m_default_material_instance =
		m_hub.create<MaterialInstance>(std::move(default_material_instance));
}


RenderForge::~RenderForge()
{
	for (auto sampler : m_sampler_bind.types) {
		if (sampler.id != SG_INVALID_ID) {
			sg_destroy_sampler(sampler);
		}
	}

	if (m_targets.mask_draw_view.id)
		sg_destroy_view(m_targets.mask_draw_view);
	if (m_targets.mask_smpl_view.id)
		sg_destroy_view(m_targets.mask_smpl_view);
	if (m_targets.dilate_draw_view.id)
		sg_destroy_view(m_targets.dilate_draw_view);
	if (m_targets.dilate_smpl_view.id)
		sg_destroy_view(m_targets.dilate_smpl_view);

	if (m_targets.mask_img.id)
		sg_destroy_image(m_targets.mask_img);
	if (m_targets.dilate_img.id)
		sg_destroy_image(m_targets.dilate_img);
}


bool RenderForge::on_event(Event& event)
{
	EventDispatcher dispatcher(event);

	return dispatcher.dispatch<ResizeEvent>(
		[this](const ResizeEvent& evt) -> bool
		{
			m_surface_info = evt.surface_info;
			return false;
		}
	);
}


void RenderForge::init_pipeline_scene()
{
	const sg_shader_desc* shader_desc = scene_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.layout.buffers[0].stride    = sizeof(SceneVertex);
	pipeline_desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;

	pipeline_desc.layout.attrs[ATTR_scene_main_pos_in].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_scene_main_pos_in].offset = offsetof(SceneVertex, pos);

	pipeline_desc.layout.attrs[ATTR_scene_main_nrm_in].format = SG_VERTEXFORMAT_UINT10_N2;
	pipeline_desc.layout.attrs[ATTR_scene_main_nrm_in].offset = offsetof(SceneVertex, nrm);

	pipeline_desc.layout.attrs[ATTR_scene_main_tan_in].format = SG_VERTEXFORMAT_UINT10_N2;
	pipeline_desc.layout.attrs[ATTR_scene_main_tan_in].offset = offsetof(SceneVertex, tan);

	pipeline_desc.layout.attrs[ATTR_scene_main_uv0_in].format = SG_VERTEXFORMAT_USHORT2N;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv0_in].offset = offsetof(SceneVertex, uv0);

	pipeline_desc.layout.attrs[ATTR_scene_main_uv1_in].format = SG_VERTEXFORMAT_USHORT2N;
	pipeline_desc.layout.attrs[ATTR_scene_main_uv1_in].offset = offsetof(SceneVertex, uv1);

	pipeline_desc.layout.attrs[ATTR_scene_main_rgb_in].format = SG_VERTEXFORMAT_UBYTE4N;
	pipeline_desc.layout.attrs[ATTR_scene_main_rgb_in].offset = offsetof(SceneVertex, rgb);

	pipeline_desc.index_type          = SG_INDEXTYPE_UINT32;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline_desc.depth.write_enabled = true;
	pipeline_desc.cull_mode           = SG_CULLMODE_BACK;
	pipeline_desc.face_winding        = SG_FACEWINDING_CCW;
	pipeline_desc.sample_count        = m_surface_info.sample_count;

	m_pipelines.scene_pbr = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_cue_solid()
{
	const sg_shader_desc* shader_desc = cue_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.layout.buffers[0].stride = sizeof(GenericVertex);

	pipeline_desc.layout.attrs[ATTR_cue_main_pos_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_cue_main_pos_in].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_cue_main_pos_in].offset = offsetof(GenericVertex, pos);

	pipeline_desc.layout.attrs[ATTR_cue_main_uv_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_cue_main_uv_in].format = SG_VERTEXFORMAT_USHORT2N;
	pipeline_desc.layout.attrs[ATTR_cue_main_uv_in].offset = offsetof(GenericVertex, uv);

	pipeline_desc.index_type          = SG_INDEXTYPE_UINT32;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline_desc.depth.write_enabled = true;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;
	pipeline_desc.face_winding        = SG_FACEWINDING_CCW;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.cue = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_cue_wire()
{
	const sg_shader_desc* shader_desc = cue_wire_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.index_type          = SG_INDEXTYPE_NONE; 
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline_desc.depth.write_enabled = true;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.cue_wire = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_bitmap()
{
	const sg_shader_desc* shader_desc = bitmap_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.layout.buffers[0].stride = sizeof(BitmapVertex);

	pipeline_desc.layout.attrs[ATTR_bitmap_main_position_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_position_in].format = SG_VERTEXFORMAT_FLOAT2;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_position_in].offset = offsetof(BitmapVertex, pos);

	pipeline_desc.layout.attrs[ATTR_bitmap_main_uv_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_uv_in].format = SG_VERTEXFORMAT_USHORT2N;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_uv_in].offset = offsetof(BitmapVertex, uv);

	pipeline_desc.layout.attrs[ATTR_bitmap_main_color_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_color_in].format = SG_VERTEXFORMAT_UBYTE4N;
	pipeline_desc.layout.attrs[ATTR_bitmap_main_color_in].offset = offsetof(BitmapVertex, rgb);

	pipeline_desc.index_type          = SG_INDEXTYPE_UINT16;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled = false;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.bitmap = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_overlay_solid()
{
	const sg_shader_desc* shader_desc = overlay_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.layout.buffers[0].stride = sizeof(GenericVertex);

	pipeline_desc.layout.attrs[ATTR_overlay_main_pos_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_overlay_main_pos_in].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_overlay_main_pos_in].offset = offsetof(GenericVertex, pos);

	pipeline_desc.layout.attrs[ATTR_overlay_main_uv_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_overlay_main_uv_in].format = SG_VERTEXFORMAT_USHORT2N;
	pipeline_desc.layout.attrs[ATTR_overlay_main_uv_in].offset = offsetof(GenericVertex, uv);

	pipeline_desc.index_type          = SG_INDEXTYPE_UINT32;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled = false;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;
	pipeline_desc.face_winding        = SG_FACEWINDING_CCW;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = m_surface_info.depth_format;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.overlay = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_overlay_wire()
{
	const sg_shader_desc* shader_desc = overlay_wire_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.index_type          = SG_INDEXTYPE_NONE; 
	
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled = false;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = m_surface_info.depth_format;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.overlay_wire = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_grid()
{
	const sg_shader_desc* shader_desc = grid_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.index_type          = SG_INDEXTYPE_NONE;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_LESS_EQUAL;
	pipeline_desc.depth.write_enabled = false;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.grid = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_mask()
{
	const sg_shader_desc* shader_desc = outline_mask_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.layout.buffers[0].stride = static_cast<int>(sizeof(SceneVertex));

	pipeline_desc.layout.attrs[ATTR_outline_mask_main_pos_in].buffer_index = 0;
	pipeline_desc.layout.attrs[ATTR_outline_mask_main_pos_in].format = SG_VERTEXFORMAT_FLOAT3;
	pipeline_desc.layout.attrs[ATTR_outline_mask_main_pos_in].offset = static_cast<int>(offsetof(SceneVertex, pos));

	pipeline_desc.index_type              = SG_INDEXTYPE_UINT32;
	pipeline_desc.depth.compare           = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled     = false;
	pipeline_desc.cull_mode               = SG_CULLMODE_NONE;
	pipeline_desc.colors[0].blend.enabled = false;
	pipeline_desc.colors[0].pixel_format  = SG_PIXELFORMAT_R8;
	pipeline_desc.depth.pixel_format      = SG_PIXELFORMAT_NONE;
	pipeline_desc.sample_count            = 1;

	m_pipelines.outline_mask = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_dilate()
{
	const sg_shader_desc* shader_desc = outline_dilate_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.index_type              = SG_INDEXTYPE_NONE;
	pipeline_desc.depth.compare           = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled     = false;
	pipeline_desc.cull_mode               = SG_CULLMODE_NONE;
	pipeline_desc.colors[0].blend.enabled = false;
	pipeline_desc.colors[0].pixel_format  = SG_PIXELFORMAT_R8;
	pipeline_desc.depth.pixel_format      = SG_PIXELFORMAT_NONE;
	pipeline_desc.sample_count            = 1;

	m_pipelines.outline_dilate = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


void RenderForge::init_pipeline_blend()
{
	const sg_shader_desc* shader_desc = outline_blend_main_shader_desc(sg_query_backend());
	sg_shader shader = sg_make_shader(shader_desc);

	sg_pipeline_desc pipeline_desc {};
	pipeline_desc.shader = shader;

	pipeline_desc.index_type          = SG_INDEXTYPE_NONE;
	pipeline_desc.depth.compare       = SG_COMPAREFUNC_ALWAYS;
	pipeline_desc.depth.write_enabled = false;
	pipeline_desc.cull_mode           = SG_CULLMODE_NONE;

	pipeline_desc.colors[0].blend.enabled          = true;
	pipeline_desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_rgb           = SG_BLENDOP_ADD;
	pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
	pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	pipeline_desc.colors[0].blend.op_alpha         = SG_BLENDOP_ADD;

	pipeline_desc.colors[0].pixel_format = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.depth.pixel_format     = _SG_PIXELFORMAT_DEFAULT;
	pipeline_desc.sample_count           = m_surface_info.sample_count;

	m_pipelines.outline_blend = {
		.shader   = shader,
		.pipeline = sg_make_pipeline(&pipeline_desc)
	};
}


Model RenderForge::create_model(const res::ImportModel& import_model)
{
	Model model;

	const uint32_t mesh_count = static_cast<uint32_t>(import_model.meshes.size());
	const uint32_t mat_count  = static_cast<uint32_t>(import_model.materials.size());

	model.meshes.resize(mesh_count);
	model.materials.resize(mat_count);

	for (uint32_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
		const res::ImportMesh& import_mesh = import_model.meshes[mesh_idx];

		auto& pos_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::pos)];
		auto& nrm_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::nrm)];
		auto& tan_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::tan)];
		auto& uv0_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::uv0)];
		auto& uv1_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::uv1)];
		auto& rgb_attr = import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::rgb)];

		const vec3* pos_data = reinterpret_cast<const vec3*>(pos_attr.blob.data());
		const vec3* nrm_data = reinterpret_cast<const vec3*>(nrm_attr.blob.data());

		const vec4* tan_data = tan_attr.blob.empty()
			? nullptr
			: reinterpret_cast<const vec4*>(tan_attr.blob.data());
		const vec2* uv0_data = uv0_attr.blob.empty()
			? nullptr
			: reinterpret_cast<const vec2*>(uv0_attr.blob.data());
		const vec2* uv1_data = uv1_attr.blob.empty()
			? nullptr
			: reinterpret_cast<const vec2*>(uv1_attr.blob.data());
		const vec4* rgb_data = rgb_attr.blob.empty()
			? nullptr
			: reinterpret_cast<const vec4*>(rgb_attr.blob.data());

		const uint32_t vtx_count = static_cast<uint32_t>(pos_attr.blob.size() / sizeof(vec3));

		mtp::vault<SceneVertex, mtp::default_set> packed_vertices;
		packed_vertices.resize(vtx_count);

		for (uint32_t vtx_index = 0; vtx_index < vtx_count; ++vtx_index) {
			SceneVertex& vertex = packed_vertices[vtx_index];

			vertex.pos = {pos_data[vtx_index].x, pos_data[vtx_index].y, pos_data[vtx_index].z};
			vertex.nrm = pack_1010102(vec4(nrm_data[vtx_index], 1.0f));

			vertex.tan = tan_data ?
				pack_1010102(tan_data[vtx_index]) : pack_1010102(vec4(0, 0, 0, 1));
			vertex.uv0 = uv0_data ?
				pack_uv(uv0_data[vtx_index])      : v2u16 {0, 0};
			vertex.uv1 = uv1_data ?
				pack_uv(uv1_data[vtx_index])      : v2u16 {0, 0};
			vertex.rgb = rgb_data ?
				pack_color(rgb_data[vtx_index])   : 0xFFFFFFFF;
		}

		model.meshes[mesh_idx] = create_mesh<SceneVertex>(
			packed_vertices,
			import_mesh.indices,
			import_mesh.submeshes
		);
	}

	for (uint32_t mat_idx = 0; mat_idx < mat_count; ++mat_idx) {
		Handle<res::MaterialResource> hnd_res = import_model.materials[mat_idx];
		Handle<MaterialTemplate>      hnd_tpl = create_material_template(hnd_res);

		model.materials[mat_idx] = create_material_instance(hnd_res, hnd_tpl);
	}

	/* pack twin wireframe */

	if (import_model.is_occluder) {

		const uint32_t twin_base_vtx = static_cast<uint32_t>(model.twin_positions.size());
		const uint32_t twin_base_idx = static_cast<uint32_t>(model.twin_indices.size());

		model.twin_positions.resize(twin_base_vtx + import_model.twin_positions.size());
		std::memcpy(
			model.twin_positions.data() + twin_base_vtx,
			import_model.twin_positions.data(),
			import_model.twin_positions.size() * sizeof(vec3)
		);

		model.twin_indices.resize(twin_base_idx + import_model.twin_indices.size());
		std::memcpy(
			model.twin_indices.data() + twin_base_idx,
			import_model.twin_indices.data(),
			import_model.twin_indices.size() * sizeof(uint32_t)
		);

		model.twin_geoslice = {
			.vtx_base  = twin_base_vtx,
			.vtx_count = static_cast<uint32_t>(import_model.twin_positions.size()),
			.idx_first = twin_base_idx,
			.idx_count = static_cast<uint32_t>(import_model.twin_indices.size())
		};

		mtp::vault<uint32_t, mtp::default_set> twin_edge_indices;
		geo::extract_unique_edges(
			model.twin_indices.data() + twin_base_idx,
			static_cast<uint32_t>(import_model.twin_indices.size()),
			twin_edge_indices
		);

		mtp::vault<rdr::GenericVertex, mtp::default_set> twin_edge_vertices;
		twin_edge_vertices.resize(import_model.occluder_twin.vtx_count);

		for (uint32_t v = 0; v < import_model.occluder_twin.vtx_count; ++v) {
			vec3 pos = import_model.twin_positions[v];

			twin_edge_vertices[v].pos.x = pos.x;
			twin_edge_vertices[v].pos.y = pos.y;
			twin_edge_vertices[v].pos.z = pos.z;

			twin_edge_vertices[v].uv = {0, 0};
		}

		model.twin_subwire = push_submesh<MassDomain::storage, GenericVertex, uint32_t>(
			twin_edge_vertices,
			twin_edge_indices
		);
	}

	/* pack hull wireframes */

	for (const auto& import_hull : import_model.hulls) {

		uint32_t vtx_base = static_cast<uint32_t>(model.hull_positions.size());
		uint32_t idx_base = static_cast<uint32_t>(model.hull_indices.size());

		model.hull_positions.resize(vtx_base + import_hull.vtx_count);
		std::memcpy(
			model.hull_positions.data() + vtx_base,
			&import_model.hull_positions[import_hull.vtx_base],
			import_hull.vtx_count * sizeof(vec3)
		);

		model.hull_indices.resize(idx_base + import_hull.idx_count);
		std::memcpy(
			model.hull_indices.data() + idx_base,
			&import_model.hull_indices[import_hull.idx_base],
			import_hull.idx_count * sizeof(uint32_t)
		);

		model.hull_geoslices.push_back({
			.vtx_base  = vtx_base,
			.vtx_count = import_hull.vtx_count,
			.idx_first = idx_base,
			.idx_count = import_hull.idx_count
		});

		mtp::vault<uint32_t, mtp::default_set> hull_edge_idxs;
		geo::extract_unique_edges(
			&model.hull_indices[idx_base],
			import_hull.idx_count,
			hull_edge_idxs
		);

		mtp::vault<rdr::GenericVertex, mtp::default_set> hull_edge_vtxs;
		hull_edge_vtxs.resize(import_hull.vtx_count);

		for (uint32_t v = 0; v < import_hull.vtx_count; ++v) {
			vec3 pos = model.hull_positions[vtx_base + v];

			hull_edge_vtxs[v].pos.x = pos.x;
			hull_edge_vtxs[v].pos.y = pos.y;
			hull_edge_vtxs[v].pos.z = pos.z;

			hull_edge_vtxs[v].uv = {0, 0};
		}

		model.hull_subwires.push_back(
			push_submesh<MassDomain::storage, GenericVertex, uint32_t>(
				hull_edge_vtxs,
				hull_edge_idxs
			)
		);
	}

	return model;
}


void RenderForge::emit_primitives(
	ecs::Entity             entity,
	const res::ImportModel& import_model,
	const Model&            model,
	scn::SceneRenderRig&    render_rig
)
{
	const uint32_t node_count = static_cast<uint32_t>(import_model.nodes.size());

	mtp::vault<mat4, mtp::default_set> node_matrices;
	node_matrices.resize(node_count);

	for (uint32_t node_idx = 0; node_idx < node_count; ++node_idx) {
		const auto& node = import_model.nodes[node_idx];

		mat4 mtx_parent = (node.parent != 0xFFFFFFFFU) 
			? node_matrices[node.parent]
			: mat4(1.0f);

		node_matrices[node_idx] = mtx_parent * node.mtx_trs;

		if (node.mesh != 0xFFFFFFFFU) {
			const auto& import_mesh = import_model.meshes[node.mesh];
			const auto* mesh        = m_hub.get<Mesh>(model.meshes[node.mesh]);

			const vec3* pos_data = reinterpret_cast<const vec3*>(
				import_mesh.vtx_attributes[static_cast<size_t>(res::AttrType::pos)].blob.data()
			);

			for (uint32_t sbm_idx = 0; sbm_idx < import_mesh.submeshes.size(); ++sbm_idx) {
				const auto& import_submesh = import_mesh.submeshes[sbm_idx];

				vec3 aabb_min( std::numeric_limits<float>::max());
				vec3 aabb_max(-std::numeric_limits<float>::max());

				for (uint32_t i = 0; i < import_submesh.idx_count; ++i) {
					uint32_t raw_idx = import_mesh.indices[import_submesh.idx_first + i];

					vec3 baked_pos = vec3(
						node_matrices[node_idx] * vec4(pos_data[raw_idx + import_submesh.vtx_base], 1.0f)
					);

					aabb_min = glm::min(aabb_min, baked_pos);
					aabb_max = glm::max(aabb_max, baked_pos);
				}

				render_rig.primitives.emplace_back(scn::ScenePrimitive {
					.submesh = {
						mesh->vtx_base  + import_submesh.vtx_base,
						mesh->idx_first + import_submesh.idx_first,
						import_submesh.idx_count
					},
					.mtx_L        = node_matrices[node_idx],
					.mtx_LN       = glm::inverse(glm::transpose(mat3(node_matrices[node_idx]))),
					.material_idx = m_hub.get<MaterialInstance>(
						model.materials[import_submesh.model_mat_idx]
					)->ssbo_idx,
					.entity = entity
				});

				render_rig.matrices_M.push_back(mat4(1.0f));
				render_rig.aabb_local.push_back({aabb_min, aabb_max});
				render_rig.occludee_idxs.push_back(model.occludee_hull_base_idx + import_submesh.hull_idx);
			}
		}
	}
}


Handle<MaterialTemplate> RenderForge::create_material_template(
	Handle<res::MaterialResource> hnd_mat_templ_res
)
{
	HPR_ASSERT(hnd_mat_templ_res.is_valid());

	const res::MaterialResource* mat_template_res =
		m_resolver.resolve<res::MaterialResource>(hnd_mat_templ_res);

	MaterialTemplate material_template {};

	auto make_template_texture =
		[this](Handle<res::ImageResource> hnd_image, PixelFormat pix_format) -> Handle<Texture>
		{
			const res::ImageResource* image = m_resolver.resolve<res::ImageResource>(hnd_image);

			if (!image) {
				HPR_WARN(
					log::LogCategory::render,
					"[forge][create_material_template] tex resolve fail [handle %u][format %u]",
					static_cast<uint32_t>(hnd_image.index),
					static_cast<uint8_t>(pix_format)
				);
				return Handle<Texture>::null();
			}

			TexKey tex_key {
				.source_key   = static_cast<uint64_t>(hnd_image.index),
				.sampler_key  = static_cast<uint64_t>(SamplerType::linear_repeat),
				.pixel_format = pix_format
			};

			if (auto cached = m_cache.find_texture(tex_key); cached.is_valid()) {
				return cached;
			}

			Handle<Texture> texture = create_texture(
				image->pixels.data(),
				image->width,
				image->height,
				pix_format
			);

			if (texture.is_valid()) {
				m_cache.put_texture(tex_key, texture);
				HPR_DEBUG(
					log::LogCategory::render,
					"[forge][create_material_template] tex created [%ux%u][format %u][index %u]",
					static_cast<uint32_t>(image->width),
					static_cast<uint32_t>(image->height),
					static_cast<uint8_t>(pix_format),
					static_cast<uint32_t>(texture.index)
				);
			}
			else {
				HPR_ERROR(
					log::LogCategory::render,
					"[forge][create_material_template] create tex fail [handle %u][format %u]",
					static_cast<uint32_t>(hnd_image.index),
					static_cast<uint8_t>(pix_format)
				);
			}
			return texture;
		};

	const auto default_template = m_hub.get<MaterialTemplate>(m_default_material_template);

	Handle<Texture> hnd_tex_albedo {};
	if (mat_template_res && mat_template_res->has_albedo()) {
		hnd_tex_albedo =
			make_template_texture(
				mat_template_res->textures[res::tex_albedo],
				PixelFormat::rgba8_srgb
			);
	}
	if (!hnd_tex_albedo.is_valid()) {
		hnd_tex_albedo = default_template->textures[static_cast<size_t>(MatMapSlot::alb)];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [albedo %u]",
			static_cast<uint32_t>(hnd_tex_albedo.index)
		);
	}

	Handle<Texture> hnd_tex_normal {};
	if (mat_template_res && mat_template_res->has_normal()) {
		hnd_tex_normal = make_template_texture(mat_template_res->textures[res::tex_normal], PixelFormat::rgba8_unorm);
	}
	if (!hnd_tex_normal.is_valid()) {
		hnd_tex_normal = default_template->textures[static_cast<size_t>(MatMapSlot::nrm)];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [normal %u]",
			static_cast<uint32_t>(hnd_tex_normal.index)
		);
	}

	Handle<Texture> hnd_tex_orm {};
	if (mat_template_res && mat_template_res->has_ormh()) {
		hnd_tex_orm = make_template_texture(mat_template_res->textures[res::tex_ormh], PixelFormat::rgba8_unorm);
	}
	if (!hnd_tex_orm.is_valid()) {
		hnd_tex_orm = default_template->textures[static_cast<size_t>(MatMapSlot::orm)];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [orm %u]",
			static_cast<uint32_t>(hnd_tex_orm.index)
		);
	}

	Handle<Texture> hnd_tex_emissive {};
	if (mat_template_res && mat_template_res->has_emissive()) {
		hnd_tex_emissive = make_template_texture(mat_template_res->textures[res::tex_emissive], PixelFormat::rgba8_srgb);
	}
	if (!hnd_tex_emissive.is_valid()) {
		hnd_tex_emissive = default_template->textures[static_cast<size_t>(MatMapSlot::ems)];
		HPR_WARN(
			log::LogCategory::render,
			"[forge][create_material_template] tex fallback [emissive %u]",
			static_cast<uint32_t>(hnd_tex_emissive.index)
		);
	}

	material_template.textures[static_cast<size_t>(MatMapSlot::alb)] = hnd_tex_albedo;
	material_template.textures[static_cast<size_t>(MatMapSlot::nrm)] = hnd_tex_normal;
	material_template.textures[static_cast<size_t>(MatMapSlot::orm)] = hnd_tex_orm;
	material_template.textures[static_cast<size_t>(MatMapSlot::ems)] = hnd_tex_emissive;

	for (int slot = 0; slot < cfg::max_tex_per_mat; ++slot) {
		material_template.uv_index[slot] = mat_template_res ? static_cast<uint8_t>(mat_template_res->uv_index[slot]) : 0;
	}

	material_template.map_mask = mat_template_res ? mat_template_res->map_mask : 0;

	HPR_DEBUG(
		log::LogCategory::render,
		"[forge][create_material_template] mat tpl mask [res %u]",
		material_template.map_mask
	);

	return m_hub.create<MaterialTemplate>(std::move(material_template));
}


Handle<MaterialInstance> RenderForge::create_material_instance(
	Handle<res::MaterialResource> hnd_mat_tmpl_res,
	Handle<MaterialTemplate>      hnd_mat_template
)
{
	assert(hnd_mat_tmpl_res.is_valid());
	assert(hnd_mat_template.is_valid());

	MaterialInstance mat_instance_cpu {};
	mat_instance_cpu.mat_template = hnd_mat_template;
	mat_instance_cpu.map_mask = m_hub.get<MaterialTemplate>(hnd_mat_template)->map_mask;

	const res::MaterialResource* mat_template_res =
		m_resolver.resolve<res::MaterialResource>(hnd_mat_tmpl_res);

	if (mat_template_res) {
		mat_instance_cpu.albedo_tint      = mat_template_res->albedo_tint;
		mat_instance_cpu.emissive_factor  = mat_template_res->emissive_factor;

		mat_instance_cpu.metallic_factor  = mat_template_res->metallic_factor;
		mat_instance_cpu.roughness_factor = mat_template_res->roughness_factor;
		mat_instance_cpu.ao_strength      = mat_template_res->ao_strength;
		mat_instance_cpu.normal_scale     = mat_template_res->normal_scale;
	}
	else {
		mat_instance_cpu.albedo_tint      = vec4(1.0f, 1.0f, 1.0f, 1.0f);
		mat_instance_cpu.emissive_factor  = vec3(0.0f, 0.0f, 0.0f);

		mat_instance_cpu.metallic_factor  = 0.0f;
		mat_instance_cpu.roughness_factor = 1.0f;
		mat_instance_cpu.ao_strength      = 1.0f;
		mat_instance_cpu.normal_scale     = 1.0f;
	}

	mat_instance_cpu.uv_scale  = vec2(1.0f, 1.0f);
	mat_instance_cpu.uv_offset = vec2(0.0f, 0.0f);

	const MaterialTemplate* mat_templ = m_hub.get<MaterialTemplate>(hnd_mat_template);
	MaterialBlob mat_inst_ssbo {};

	mat_inst_ssbo.alb = mat_instance_cpu.albedo_tint;
	mat_inst_ssbo.ems_mtl = vec4(
		mat_instance_cpu.emissive_factor,
		mat_instance_cpu.metallic_factor
	);
	mat_inst_ssbo.rgh_nrm_aos_map = vec4(
		mat_instance_cpu.roughness_factor,
		mat_instance_cpu.normal_scale,
		mat_instance_cpu.ao_strength,
		static_cast<float>(mat_instance_cpu.map_mask)
	);
	mat_inst_ssbo.uv_scale_offset = vec4(
		mat_instance_cpu.uv_scale,
		mat_instance_cpu.uv_offset
	);

	auto fill_tex = [this, mat_templ](v4u32& packed, MatMapSlot map)
	{
		const size_t tex_idx = static_cast<size_t>(map);
		const Texture* tex   = m_hub.get<Texture>(mat_templ->textures[tex_idx]);

		packed.x = static_cast<uint32_t>(tex->array);
		packed.y = static_cast<uint32_t>(tex->slice);
		packed.z = static_cast<uint32_t>(mat_templ->uv_index[tex_idx]);
		packed.w = 0;
	};

	fill_tex(mat_inst_ssbo.tex_info_alb, MatMapSlot::alb);
	fill_tex(mat_inst_ssbo.tex_info_nrm, MatMapSlot::nrm);
	fill_tex(mat_inst_ssbo.tex_info_orm, MatMapSlot::orm);
	fill_tex(mat_inst_ssbo.tex_info_ems, MatMapSlot::ems);

	mat_instance_cpu.ssbo_idx = m_mat_inst_mass.push_staged(mat_inst_ssbo);

	return m_hub.create<MaterialInstance>(std::move(mat_instance_cpu));
}


Handle<Texture> RenderForge::create_texture(
	const void* pix_data,
	uint32_t    width,
	uint32_t    height,
	PixelFormat pix_format
)
{
	HPR_ASSERT(pix_data);
	HPR_ASSERT(width  > 0);
	HPR_ASSERT(height > 0);

	TextureMassSlice slice = m_texture_mass.stage(
		pix_data,
		width,
		height,
		pix_format
	);

	Texture texture {
		.array  = slice.array,
		.slice  = slice.slice,
		.width  = slice.width,
		.height = slice.height
	};

	return m_hub.create<Texture>(std::move(texture));
}


Handle<Texture> RenderForge::create_tilemap(
	uint32_t width,
	uint32_t height
)
{
	HPR_ASSERT(width  > 0);
	HPR_ASSERT(height > 0);

	TileMassSlice slice = m_tilemap_mass.stage(width, height);

	Texture texture {
		.array  = 0xFFFFFFFF,
		.slice  = slice.atlas,
		.width  = slice.width,
		.height = slice.height
	};

	return m_hub.create<Texture>(std::move(texture));
}


Handle<Texture> RenderForge::create_palette()
{
	static constexpr uint32_t colors_per_palette = 256;
	static constexpr uint32_t num_palettes       = 1;

	struct RGB { uint8_t r, g, b; };
	static constexpr std::array<RGB, 8> protozerg_colors = {{

		{128,   0, 255}, // dracula violet
		{214,   0, 147}, // vampire fuchsia
		{0,    85, 255}, // grid blue
		{0,   238, 255}, // neon cyan
		{0,   255, 170}, // matrix mint
		{43,  255, 119}, // biogreen
		{212, 255,   0}, // acid yellow
		{255,   0, 128}  // synthwave pink
	}};

	std::array<uint8_t, colors_per_palette * 4> palette_data {};

	for (uint32_t i = 0; i < colors_per_palette; ++i) {
		const auto& color = protozerg_colors[i % 8];
		
		palette_data[i * 4 + 0] = color.r;
		palette_data[i * 4 + 1] = color.g;
		palette_data[i * 4 + 2] = color.b;
		palette_data[i * 4 + 3] = 26;
	}

	m_palette_mass.init(palette_data.data(), num_palettes, colors_per_palette);

	Texture texture {
		.array  = 0xFFFFFFFF,
		.slice  = 0,
		.width  = colors_per_palette,
		.height = 1
	};

	return m_hub.create<Texture>(std::move(texture));
}


void RenderForge::update_tilemap(
	Handle<rdr::Texture>      tilemap_hnd,
	std::span<const uint16_t> tilemap_bytes,
	int32_t                   width,
	int32_t                   height
)
{
	Texture* texture = m_hub.get<rdr::Texture>(tilemap_hnd);
	if (!texture)
		return;

	HPR_ASSERT_MSG(width  > 0,
		"[forge] tilemap width <= 0"
	);
	HPR_ASSERT_MSG(height > 0,
		"[forge] tilemap height <= 0"
	);
	HPR_ASSERT_MSG(width == texture->width && height == texture->height,
		"[forge] tilemap size mismatch"
	);

	const size_t area_size = static_cast<size_t>(width) * static_cast<size_t>(height);
	HPR_ASSERT_MSG(tilemap_bytes.size() == area_size,
		"[forge] tilemap data size mismatch"
	);

	/*
	sg_image_data data {};
	data.mip_levels[0].ptr  = tilemap_bytes.data();
	data.mip_levels[0].size = tilemap_bytes.size_bytes();

	sg_update_image(texture->image, &data);
	*/
}


Handle<Font> RenderForge::create_bitmap_font(
	const FontSpec&            spec,
	Handle<res::ImageResource> atlas_image,
	const FontMetrics&         metrics
)
{
	const res::ImageResource* image = m_resolver.resolve(atlas_image);
	if (!image)
		return Handle<Font>::null();

	if (image->width == 0 || image->height == 0 || image->channels != 1)
		return Handle<Font>::null();

	const size_t size_expect =
		static_cast<size_t>(image->width) *
		static_cast<size_t>(image->height);

	if (image->pixels.size() != size_expect)
		return Handle<Font>::null();

	uint32_t atlas_idx = m_font_mass.add_atlas(
		image->pixels.data(),
		image->width,
		image->height
	);

	Font font {
		.spec      = spec,
		.metrics   = metrics,
		.atlas_idx = atlas_idx
	};

	return m_hub.create<Font>(std::move(font));
}


void RenderForge::update_nuklear_atlas(const void* pixels, int width, int height)
{
	if (m_nk_atlas.image.id) {
		sg_destroy_image(m_nk_atlas.image);
		sg_destroy_view(m_nk_atlas.view);
	}

	sg_image_desc img_desc {};
	img_desc.type         = SG_IMAGETYPE_2D;
	img_desc.width        = width;
	img_desc.height       = height;
	img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
	img_desc.num_mipmaps  = 1;
	img_desc.data.mip_levels[0].ptr  = pixels;
	img_desc.data.mip_levels[0].size =
		static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

	m_nk_atlas.image = sg_make_image(&img_desc);

	sg_view_desc view_desc {};
	view_desc.texture.image = m_nk_atlas.image;
	m_nk_atlas.view = sg_make_view(&view_desc);
}


void RenderForge::create_samplers()
{
	auto make_sampler = [](sg_filter filter, sg_wrap wrap) {
		sg_sampler_desc smp_desc {};

		smp_desc.min_filter = filter;
		smp_desc.mag_filter = filter;
		smp_desc.wrap_u     = wrap;
		smp_desc.wrap_v     = wrap;
		smp_desc.wrap_w     = wrap;

		return sg_make_sampler(&smp_desc);
	};

	auto& samplers = m_sampler_bind.types;

	samplers[static_cast<size_t>(SamplerType::linear_repeat)] =
		make_sampler(SG_FILTER_LINEAR, SG_WRAP_REPEAT);

	samplers[static_cast<size_t>(SamplerType::linear_clamp)] =
		make_sampler(SG_FILTER_LINEAR, SG_WRAP_CLAMP_TO_EDGE);

	samplers[static_cast<size_t>(SamplerType::nearest_repeat)] =
		make_sampler(SG_FILTER_NEAREST, SG_WRAP_REPEAT);

	samplers[static_cast<size_t>(SamplerType::nearest_clamp)] =
		make_sampler(SG_FILTER_NEAREST, SG_WRAP_CLAMP_TO_EDGE);
}


void RenderForge::create_targets()
{
	sg_image_desc img_desc {};
	img_desc.width        = static_cast<int>(m_surface_info.width);
	img_desc.height       = static_cast<int>(m_surface_info.height);
	img_desc.pixel_format = SG_PIXELFORMAT_R8;
	img_desc.label        = "outline_target";
	img_desc.usage.color_attachment = true;

	m_targets.mask_img   = sg_make_image(&img_desc);
	m_targets.dilate_img = sg_make_image(&img_desc);

	sg_view_desc mask_draw_desc {};
	mask_draw_desc.color_attachment.image = m_targets.mask_img;
	m_targets.mask_draw_view = sg_make_view(&mask_draw_desc);

	sg_view_desc mask_smpl_desc {};
	mask_smpl_desc.texture.image = m_targets.mask_img;
	m_targets.mask_smpl_view = sg_make_view(&mask_smpl_desc);

	sg_view_desc dilate_draw_desc {};
	dilate_draw_desc.color_attachment.image = m_targets.dilate_img;
	m_targets.dilate_draw_view = sg_make_view(&dilate_draw_desc);

	sg_view_desc dilate_smpl_desc {};
	dilate_smpl_desc.texture.image = m_targets.dilate_img;
	m_targets.dilate_smpl_view = sg_make_view(&dilate_smpl_desc);
}


BindingContext RenderForge::binding_context()
{
	BindingContext binding_ctx {

		.fonts     = m_font_mass.bind_state(),
		.texarrays = m_texture_mass.bind_state(),
		.tilemaps  = m_tilemap_mass.bind_state(),
		.palettes  = m_palette_mass.bind_state(),

		.samplers  = m_sampler_bind,

		.scn_vtx   = m_scene_mass.bind_state(),
		.gen_vtx   = m_generic_mass.bind_state(),
		.btm_vtx   = m_bitmap_mass.bind_state(),

		.scn_trs   = m_scene_trs_mass.bind_state(),
		.cue_trs   = m_cue_trs_mass.bind_state(),
		.vtx_ssbo  = m_vtx_ssbo_mass.bind_state(),
		.idx_ssbo  = m_idx_ssbo_mass.bind_state(),
		.orl_trs   = m_overlay_trs_mass.bind_state(),
		.mat_inst  = m_mat_inst_mass.bind_state(),

		.pipelines = m_pipelines,

		.targets   = m_targets,

		.nk_atlas  = m_nk_atlas
	};

	return binding_ctx;
}


StagingContext RenderForge::staging_context()
{
	return StagingContext {
		&m_scene_trs_mass,
		&m_cue_trs_mass,
		&m_overlay_trs_mass,
		&m_mat_inst_mass,

		&m_scene_mass,
		&m_generic_mass,
		&m_bitmap_mass
	};
}


} // hpr::rdr

