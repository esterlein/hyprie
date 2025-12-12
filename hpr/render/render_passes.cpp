#include "render_passes.hpp"

#include "math.hpp"
#include "color.hpp"
#include "fx_data.hpp"
#include "tile_data.hpp"
#include "ui_alloc.hpp"
#include "light_common.hpp"

#include "ui.glsl.h"
#include "tile.glsl.h"
#include "grid.glsl.h"
#include "overlay.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_dilate.glsl.h"
#include "outline_blend.glsl.h"

#include "log.hpp"
#include "panic.hpp"


namespace hpr::rdr {


void ScenePass::resize(const SurfaceInfo& surface_info)
{
	(void) surface_info;
}


void ScenePass::set_view_light(const rdr::DrawViewLightSet& view_light_set)
{
	std::memset(&m_light_ubo, 0, sizeof(m_light_ubo));

	const uint32_t count = (view_light_set.count > scn::k_max_light_count)
		? scn::k_max_light_count
		: view_light_set.count;

	m_light_ubo.light_count = static_cast<int>(count);

	for (size_t i = 0; i < vec3::length(); ++i)
		m_light_ubo.ambient_rgb[i] = view_light_set.ambient_rgb[i];

	for (uint32_t i = 0; i < count; ++i) {
		const auto& light_item = view_light_set.items[i];

		m_light_ubo.light_scalar_params[i][0] = static_cast<float>(light_item.type);
		m_light_ubo.light_scalar_params[i][1] = light_item.intensity;
		m_light_ubo.light_scalar_params[i][2] = light_item.range;

		for (size_t j = 0; j < vec3::length(); ++j) {
			m_light_ubo.light_color_rgb[i][j] = light_item.color_rgb[j];
			m_light_ubo.light_dir_view[i][j]  = light_item.dir_view[j];
			m_light_ubo.light_pos_view[i][j]  = light_item.pos_view[j];
		}

		m_light_ubo.light_spot_params[i][0] = light_item.cos_inner;
		m_light_ubo.light_spot_params[i][1] = light_item.cos_outer;
	}
}


void ScenePass::execute(RenderQueue<SceneDrawCommand>& queue, const FrameContext& context)
{
	m_replay_tokens.clear();

	set_view_light(context.scene_light);

	sg_pass_action pass_action {};
	pass_action.colors[0].load_action  = SG_LOADACTION_CLEAR;
	pass_action.colors[0].store_action = SG_STOREACTION_STORE;
	pass_action.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 1.0f};
	pass_action.depth.load_action      = SG_LOADACTION_CLEAR;
	pass_action.depth.store_action     = SG_STOREACTION_STORE;
	pass_action.depth.clear_value      = 1.0f;

	sg_pass pass {};
	pass.swapchain = sglue_swapchain();
	pass.action    = pass_action;

	queue.sort();

	sg_begin_pass(pass);

	uint32_t last_pipeline = 0;

	for (const auto& command : queue.commands()) {
		static constexpr int k_instance_count_one = 1;

		const Mesh* mesh = m_hub.get<Mesh>(command.mesh);
		const MaterialInstance* material_instance = m_hub.get<MaterialInstance>(command.material);
	
		HPR_ASSERT_MSG(mesh,              "[scene draw] missing mesh");
		HPR_ASSERT_MSG(material_instance, "[scene draw] missing material instance");
	
		const MaterialTemplate* material_template =
			m_hub.get<MaterialTemplate>(material_instance->mat_template);
	
		HPR_ASSERT_MSG(material_template, "[scene draw] missing material template");
	
		const Program* program = m_hub.get<Program>(material_template->program);
	
		HPR_ASSERT_MSG(program, "[scene draw] missing program");

		const Submesh& submesh = mesh->submeshes[command.submesh_idx];
		sg_bindings bindings   = mesh->bindings;

		if (submesh.idx_buffer.id) {
			bindings.index_buffer = submesh.idx_buffer;
		}

		for (uint8_t i = 0; i < cfg::max_tex_per_mat; ++i) {
			const Texture* texture = m_hub.get<Texture>(material_template->textures[i]);

			const sg_image   image   = texture ? texture->image   : sg_image {};
			const sg_sampler sampler = texture ? texture->sampler : sg_sampler {};

			bindings.images  [program->image_slots[i]]   = image;
			bindings.samplers[program->sampler_slots[i]] = sampler;
		}

		if (program->pipeline.id != last_pipeline) {

			sg_apply_pipeline(program->pipeline);

			last_pipeline = program->pipeline.id;
		}

		sg_apply_bindings(&bindings);

		scene_vs_params_t scene_vs_params {};
		const mat4 mtx_MV  = context.scene_view.mtx_V  * command.mtx_M;
		const mat4 mtx_MVP = context.scene_view.mtx_VP * command.mtx_M;

		std::memcpy(scene_vs_params.mtx_MV,  glm::value_ptr(mtx_MV),  sizeof(scene_vs_params.mtx_MV));
		std::memcpy(scene_vs_params.mtx_MVP, glm::value_ptr(mtx_MVP), sizeof(scene_vs_params.mtx_MVP));

		sg_apply_uniforms(UB_scene_vs_params, SG_RANGE(scene_vs_params));

		scene_fs_pbr_params_t scene_pbr_fs_params {};
		scene_pbr_fs_params.map_mask = material_instance->map_mask;

		std::memcpy(
			scene_pbr_fs_params.albedo_tint,
			glm::value_ptr(material_instance->albedo_tint),
			sizeof(scene_pbr_fs_params.albedo_tint)
		);
		std::memcpy(
			scene_pbr_fs_params.emissive_factor,
			glm::value_ptr(material_instance->emissive_factor),
			sizeof(scene_pbr_fs_params.emissive_factor)
		);

		scene_pbr_fs_params.metallic_factor  = material_instance->metallic_factor;
		scene_pbr_fs_params.roughness_factor = material_instance->roughness_factor;
		scene_pbr_fs_params.normal_scale     = material_instance->normal_scale;
		scene_pbr_fs_params.ao_strength      = material_instance->ao_strength;

		std::memcpy(
			scene_pbr_fs_params.uv_scale,
			glm::value_ptr(material_instance->uv_scale),
			sizeof(scene_pbr_fs_params.uv_scale)
		);
		std::memcpy(
			scene_pbr_fs_params.uv_offset,
			glm::value_ptr(material_instance->uv_offset),
			sizeof(scene_pbr_fs_params.uv_offset)
		);

		scene_pbr_fs_params.uv_index_albedo   = material_template->uv_index[Tex::alb];
		scene_pbr_fs_params.uv_index_normal   = material_template->uv_index[Tex::nrm];
		scene_pbr_fs_params.uv_index_orm      = material_template->uv_index[Tex::orm];
		scene_pbr_fs_params.uv_index_emissive = material_template->uv_index[Tex::ems];

		sg_apply_uniforms(UB_scene_fs_pbr_params,   SG_RANGE(scene_pbr_fs_params));
		sg_apply_uniforms(UB_scene_fs_light_params, SG_RANGE(m_light_ubo));

		if (command.flags & static_cast<uint8_t>(SceneDrawCmdFlag::Selected)) {
			SceneReplayToken replay_token {
				.mesh        = command.mesh,
				.submesh_idx = command.submesh_idx,
				.mtx_model   = command.mtx_M,
				.program     = material_template->program
			};
			m_replay_tokens.push_back(replay_token);
		}

		sg_draw(static_cast<int>(submesh.first_idx), static_cast<int>(submesh.idx_count), k_instance_count_one);
	}

	sg_end_pass();
}


void OutlinePass::init()
{
	sg_sampler_desc smp_nearest {};
	smp_nearest.min_filter = SG_FILTER_NEAREST;
	smp_nearest.mag_filter = SG_FILTER_NEAREST;
	smp_nearest.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	smp_nearest.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
	m_sampler_nearest      = sg_make_sampler(&smp_nearest);

	sg_sampler_desc smp_linear {};
	smp_linear.min_filter = SG_FILTER_LINEAR;
	smp_linear.mag_filter = SG_FILTER_LINEAR;
	smp_linear.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	smp_linear.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
	m_sampler_linear      = sg_make_sampler(&smp_linear);
}


void OutlinePass::recreate_images(int width, int height)
{
	if (m_attachments_mask.id) {
		sg_destroy_attachments(m_attachments_mask);
	}
	if (m_attachments_dilate.id) {
		sg_destroy_attachments(m_attachments_dilate);
	}
	if (m_image_mask.id) {
		sg_destroy_image(m_image_mask);
	}
	if (m_image_dilate.id) {
		sg_destroy_image(m_image_dilate);
	}

	sg_image_desc image_desc {};
	image_desc.width        = width;
	image_desc.height       = height;
	image_desc.pixel_format = SG_PIXELFORMAT_R8;
	image_desc.usage.render_attachment = true;

	m_image_mask   = sg_make_image(&image_desc);
	m_image_dilate = sg_make_image(&image_desc);

	sg_attachments_desc mask_desc {};
	mask_desc.colors[0].image = m_image_mask;
	m_attachments_mask = sg_make_attachments(&mask_desc);

	sg_attachments_desc dilate_desc {};
	dilate_desc.colors[0].image = m_image_dilate;
	m_attachments_dilate = sg_make_attachments(&dilate_desc);
}


void OutlinePass::resize(const SurfaceInfo& surface_info)
{
	m_width  = static_cast<int>(surface_info.width  > 0 ? surface_info.width  : 1);
	m_height = static_cast<int>(surface_info.height > 0 ? surface_info.height : 1);

	recreate_images(m_width, m_height);
}


void OutlinePass::set_programs(Handle<Program> prog_mask, Handle<Program> prog_dilate, Handle<Program> prog_blend)
{
	m_prog_mask   = prog_mask;
	m_prog_dilate = prog_dilate;
	m_prog_blend  = prog_blend;
}


void OutlinePass::set_outline_params(uint32_t rgb888, uint32_t alpha, int radius_px)
{
	m_rgb888    = rgb888;
	m_alpha     = alpha;
	m_radius_px = radius_px;
}


void OutlinePass::execute(const mtp::vault<SceneReplayToken, mtp::default_set>& replay_tokens, const FrameContext& context)
{
	if (replay_tokens.empty())
		return;

	const Program* prog_mask   = m_hub.get<Program>(m_prog_mask);
	const Program* prog_dilate = m_hub.get<Program>(m_prog_dilate);
	const Program* prog_blend  = m_hub.get<Program>(m_prog_blend);

	HPR_ASSERT_MSG(prog_mask,   "[outline draw] missing mask program");
	HPR_ASSERT_MSG(prog_dilate, "[outline draw] missing dilate program");
	HPR_ASSERT_MSG(prog_blend,  "[outline draw] missing blend program");

	/* mask pass */

	{
		sg_pass_action pass_action {};
		pass_action.colors[0].load_action  = SG_LOADACTION_CLEAR;
		pass_action.colors[0].store_action = SG_STOREACTION_STORE;
		pass_action.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 0.0f};

		sg_pass pass {};
		pass.attachments = m_attachments_mask;
		pass.action      = pass_action;

		sg_begin_pass(pass);

		for (const auto& replay_token : replay_tokens) {

			static constexpr int k_instance_count_one = 1;

			const Mesh* mesh = m_hub.get<Mesh>(replay_token.mesh);
			HPR_ASSERT_MSG(mesh, "[mask pass] missing replay mesh");

			const Submesh& submesh = mesh->submeshes[replay_token.submesh_idx];
			sg_bindings bindings   = mesh->bindings;

			if (submesh.idx_buffer.id) {
				bindings.index_buffer = submesh.idx_buffer;
			}

			sg_apply_pipeline(prog_mask->pipeline);

			sg_apply_bindings(&bindings);

			outline_mask_vs_params_t outline_mask_params {};

			const mat4 mtx_MV  = context.scene_view.mtx_V  * replay_token.mtx_model;
			const mat4 mtx_MVP = context.scene_view.mtx_VP * replay_token.mtx_model;

			std::memcpy(outline_mask_params.mtx_MV,  glm::value_ptr(mtx_MV),  sizeof(outline_mask_params.mtx_MV));
			std::memcpy(outline_mask_params.mtx_MVP, glm::value_ptr(mtx_MVP), sizeof(outline_mask_params.mtx_MVP));

			sg_apply_uniforms(UB_outline_mask_vs_params, SG_RANGE(outline_mask_params));
			sg_draw(static_cast<int>(submesh.first_idx), static_cast<int>(submesh.idx_count), k_instance_count_one);
		}

		sg_end_pass();
	}

	/* dilate pass */

	{
		sg_pass_action pass_action {};
		pass_action.colors[0].load_action  = SG_LOADACTION_CLEAR;
		pass_action.colors[0].store_action = SG_STOREACTION_STORE;
		pass_action.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 0.0f};

		sg_pass pass {};
		pass.attachments = m_attachments_dilate;
		pass.action      = pass_action;

		sg_begin_pass(pass);

		sg_bindings bindings {};
		bindings.images  [IMG_outline_dilate_u_mask_tex] = m_image_mask;
		bindings.samplers[SMP_outline_dilate_u_mask_smp] = m_sampler_nearest;

		sg_apply_pipeline(prog_dilate->pipeline);
		sg_apply_bindings(&bindings);

		outline_dilate_fs_params_t outline_dilate_params {};
		outline_dilate_params.mask_tex_size_px[0] = float(m_width);
		outline_dilate_params.mask_tex_size_px[1] = float(m_height);
		outline_dilate_params.radius_px = m_radius_px;

		sg_apply_uniforms(UB_outline_dilate_fs_params, SG_RANGE(outline_dilate_params));
		sg_draw(0, 3, 1);

		sg_end_pass();
	}

	/* blend pass */

	{
		sg_pass_action pass_action {};
		pass_action.colors[0].load_action  = SG_LOADACTION_LOAD;
		pass_action.colors[0].store_action = SG_STOREACTION_STORE;
		pass_action.depth.load_action      = SG_LOADACTION_LOAD;

		sg_pass pass {};
		pass.swapchain = sglue_swapchain();
		pass.action    = pass_action;

		sg_begin_pass(pass);

		sg_apply_viewport(0, 0, m_width, m_height, true);

		sg_bindings bindings {};
		bindings.images  [IMG_outline_blend_u_mask_orig_tex]    = m_image_mask;
		bindings.samplers[SMP_outline_blend_u_mask_orig_smp]    = m_sampler_linear;
		bindings.images  [IMG_outline_blend_u_mask_dilated_tex] = m_image_dilate;
		bindings.samplers[SMP_outline_blend_u_mask_dilated_smp] = m_sampler_linear;

		sg_apply_pipeline(prog_blend->pipeline);
		sg_apply_bindings(&bindings);

		outline_blend_fs_params_t outline_blend_params {};
		outline_blend_params.mask_tex_size_px[0] = float(m_width);
		outline_blend_params.mask_tex_size_px[1] = float(m_height);

		frgb_from_u32(m_rgb888, outline_blend_params.outline_color_rgb);

		const float outline_alpha = static_cast<float>(m_alpha & 0xFF) / 255.0f;
		outline_blend_params.outline_alpha = outline_alpha;

		sg_apply_uniforms(UB_outline_blend_fs_params, SG_RANGE(outline_blend_params));
		sg_draw(0, 3, 1);

		sg_end_pass();
	}
}


void OutlinePass::release()
{
	if (m_attachments_mask.id) {
		sg_destroy_attachments(m_attachments_mask);
		m_attachments_mask = {};
	}
	if (m_attachments_dilate.id) {
		sg_destroy_attachments(m_attachments_dilate);
		m_attachments_dilate = {};
	}
	if (m_image_mask.id) {
		sg_destroy_image(m_image_mask);
		m_image_mask = {};
	}
	if (m_image_dilate.id) {
		sg_destroy_image(m_image_dilate);
		m_image_dilate = {};
	}
	if (m_sampler_nearest.id) {
		sg_destroy_sampler(m_sampler_nearest);
		m_sampler_nearest = {};
	}
	if (m_sampler_linear.id) {
		sg_destroy_sampler(m_sampler_linear);
		m_sampler_linear = {};
	}
}



void CompositorPass::init()
{}


void CompositorPass::resize(const SurfaceInfo& surface_info)
{
	m_surface_info = surface_info;
}


void CompositorPass::set_programs(Handle<Program> prog_grid, Handle<Program> prog_tile, Handle<Program> prog_overlay)
{
	m_prog_grid    = prog_grid;
	m_prog_tile    = prog_tile;
	m_prog_overlay = prog_overlay;
}


void CompositorPass::execute(
	RenderQueue<FxDrawCommand>&      fx_queue,
	RenderQueue<TileDrawCommand>&     cue_queue,
	RenderQueue<OverlayDrawCommand>& overlay_queue,
	const FrameContext&              context,
	const SurfaceInfo&               surface_info
)
{
	fx_queue.sort();
	cue_queue.sort();
	overlay_queue.sort();

	sg_pass_action pass_action {};
	pass_action.colors[0].load_action = SG_LOADACTION_LOAD;
	pass_action.depth.load_action     = SG_LOADACTION_LOAD;

	sg_pass pass {};
	pass.swapchain = sglue_swapchain();
	pass.action    = pass_action;

	sg_begin_pass(pass);


	/* fx: grid */

	for (const auto& draw_command : fx_queue.commands()) {
		if (draw_command.kind == static_cast<uint8_t>(ProgramFlag::Grid)) {

			const Program* program = m_hub.get<Program>(m_prog_grid);
			HPR_ASSERT_MSG(program, "[fx grid pass] missing program");

			const auto* grid_pack = reinterpret_cast<const GridPack*>(draw_command.params.data());

			grid_fs_camera_params_t grid_camera_params {};

			const mat4 mtx_VP_inv = glm::inverse(context.scene_view.mtx_VP);

			std::memcpy(
				grid_camera_params.mtx_VP,
				glm::value_ptr(context.scene_view.mtx_VP),
				sizeof(grid_camera_params.mtx_VP)
			);
			std::memcpy(
				grid_camera_params.mtx_VP_inv,
				glm::value_ptr(mtx_VP_inv),
				sizeof(grid_camera_params.mtx_VP_inv)
			);

			grid_camera_params.framebuffer_size_px[0] = static_cast<float>(surface_info.width);
			grid_camera_params.framebuffer_size_px[1] = static_cast<float>(surface_info.height);

			grid_camera_params.cell_size_world = grid_pack->base_spacing;
			grid_camera_params.grid_plane_y    = grid_pack->grid_y;

			grid_fs_params_t grid_params {};

			grid_params.line_width_px    = grid_pack->line_width_px;
			grid_params.major_step_cells = grid_pack->major_step;

			// TODO: move all magic to config

			frgb_from_u32(grid_pack->minor_rgb888, grid_params.minor_color_rgba);
			grid_params.minor_color_rgba[3] = 1.0f;

			frgb_from_u32(grid_pack->major_rgb888, grid_params.major_color_rgba);
			grid_params.major_color_rgba[3] = 1.0f;

			grid_params.minor_visibility_range_px[0] = 2.0f;
			grid_params.minor_visibility_range_px[1] = 8.0f;
			grid_params.major_visibility_range_px[0] = 4.0f;
			grid_params.major_visibility_range_px[1] = 16.0f;

			sg_apply_viewport(0, 0, static_cast<int>(surface_info.width), static_cast<int>(surface_info.height), true);

			sg_apply_pipeline(program->pipeline);

			sg_apply_uniforms(UB_grid_fs_camera_params, SG_RANGE(grid_camera_params));
			sg_apply_uniforms(UB_grid_fs_params,        SG_RANGE(grid_params));

			sg_draw(0, 3, 1);
		}
	}


	/* tiles */

	for (const auto& tile_cmd : cue_queue.commands()) {

		const Mesh*      mesh       = m_hub.get<Mesh>(tile_cmd.mesh);
		const TileStyle* tile_style = m_hub.get<TileStyle>(tile_cmd.tile_style);
		const Program*   program    = m_hub.get<Program>(m_prog_tile);
		const Texture*   palette    = m_hub.get<Texture>(tile_style->palette);
		const Texture*   tilemap    = m_hub.get<Texture>(tile_cmd.tilemap);

		HPR_ASSERT_MSG(mesh,       "[tile pass] missing mesh");
		HPR_ASSERT_MSG(tile_style, "[tile pass] missing tile style");
		HPR_ASSERT_MSG(program,    "[tile pass] missing tile program");
		HPR_ASSERT_MSG(palette,    "[tile pass] missing palette texture");
		HPR_ASSERT_MSG(tilemap,    "[tile pass] missing tilemap texture");

		const Submesh& submesh = mesh->submeshes[tile_cmd.submesh_idx];

		sg_bindings bindings = mesh->bindings;

		if (submesh.idx_buffer.id) {
			bindings.index_buffer = submesh.idx_buffer;
		}

		bindings.images  [IMG_tile_u_palette_tex] = palette->image;
		bindings.samplers[SMP_tile_u_palette_smp] = palette->sampler;
		bindings.images  [IMG_tile_u_tilemap_tex] = tilemap->image;
		bindings.samplers[SMP_tile_u_tilemap_smp] = tilemap->sampler;

		tile_vs_view_params_t view_params {};
		std::memcpy(
			view_params.mtx_VP,
			glm::value_ptr(context.scene_view.mtx_VP),
			sizeof(view_params.mtx_VP)
		);
		std::memcpy(
			view_params.mtx_M,
			glm::value_ptr(tile_cmd.mtx_M),
			sizeof(view_params.mtx_M)
		);

		tile_fs_tile_params_t tile_params {};
		std::memcpy(
			tile_params.fill,
			glm::value_ptr(tile_style->tile_params),
			sizeof(tile_params.fill)
		);
		std::memcpy(
			tile_params.border_color,
			glm::value_ptr(tile_style->border_color),
			sizeof(tile_params.border_color)
		);

		tile_params.chunk_size = scn::cfg::chunk_size;

		sg_apply_viewport(0, 0, static_cast<int>(surface_info.width), static_cast<int>(surface_info.height), true);

		sg_apply_pipeline(program->pipeline);
		sg_apply_bindings(&bindings);
	
		sg_apply_uniforms(UB_tile_vs_view_params, SG_RANGE(view_params));
		sg_apply_uniforms(UB_tile_fs_tile_params, SG_RANGE(tile_params));

		sg_draw(static_cast<int>(submesh.first_idx), static_cast<int>(submesh.idx_count), 1);
	}


	/* overlays */

	for (const auto& draw_command : overlay_queue.commands()) {

		const Mesh* mesh       = m_hub.get<Mesh>(draw_command.mesh);
		const Program* program = m_hub.get<Program>(m_prog_overlay);

		HPR_ASSERT_MSG(mesh,    "[overlay pass] missing mesh");
		HPR_ASSERT_MSG(program, "[overlay pass] missing program");

		const Submesh& submesh = mesh->submeshes[draw_command.submesh_idx];
		sg_bindings bindings   = mesh->bindings;

		if (submesh.idx_buffer.id) {
			bindings.index_buffer = submesh.idx_buffer;
		}

		overlay_vs_params_t overlay_params {};

		std::memcpy(
			overlay_params.mtx_VP,
			glm::value_ptr(context.scene_view.mtx_VP),
			sizeof(overlay_params.mtx_VP)
		);
		std::memcpy(
			overlay_params.mtx_M,
			glm::value_ptr(draw_command.mtx_m),
			sizeof(overlay_params.mtx_M)
		);
		std::memcpy(
			overlay_params.color_rgba,
			glm::value_ptr(draw_command.rgba),
			sizeof(overlay_params.color_rgba)
		);

		sg_apply_viewport(0, 0, static_cast<int>(surface_info.width), static_cast<int>(surface_info.height), true);

		sg_apply_pipeline(program->pipeline);
		sg_apply_bindings(&bindings);

		sg_apply_uniforms(UB_overlay_vs_params, SG_RANGE(overlay_params));
		sg_draw(static_cast<int>(submesh.first_idx), static_cast<int>(submesh.idx_count), 1);
	}

	sg_end_pass();
}


void CompositorPass::release()
{}


void UiPass::init()
{
	sg_buffer_desc vtx_buf {};
	vtx_buf.size = m_vtx_capacity;
	vtx_buf.usage.vertex_buffer  = true;
	vtx_buf.usage.dynamic_update = true;
	vtx_buf.label = "ui_vtx_buf";
	m_ui_vtx_buf = sg_make_buffer(&vtx_buf);

	sg_buffer_desc idx_buf {};
	idx_buf.size = m_idx_capacity;
	idx_buf.usage.index_buffer   = true;
	idx_buf.usage.dynamic_update = true;
	idx_buf.label = "ui_idx_buf";
	m_ui_idx_buf = sg_make_buffer(&idx_buf);

	m_ui_layout[0] = {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(UiVertex, pos)};
	m_ui_layout[1] = {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    NK_OFFSETOF(UiVertex, uv)};
	m_ui_layout[2] = {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(UiVertex, col)};

	m_ui_layout[3].attribute = NK_VERTEX_ATTRIBUTE_COUNT;
	m_ui_layout[3].format    = NK_FORMAT_COUNT;
	m_ui_layout[3].offset    = 0;

	m_ui_cfg.vertex_layout        = m_ui_layout;
	m_ui_cfg.vertex_size          = sizeof(UiVertex);
	m_ui_cfg.vertex_alignment     = NK_ALIGNOF(UiVertex);
	m_ui_cfg.circle_segment_count = 22;
	m_ui_cfg.curve_segment_count  = 22;
	m_ui_cfg.arc_segment_count    = 22;
	m_ui_cfg.global_alpha         = 1.f;
	m_ui_cfg.shape_AA             = NK_ANTI_ALIASING_ON;
	m_ui_cfg.line_AA              = NK_ANTI_ALIASING_ON;

	nk_allocator ui_alloc = MetapoolNuklearAllocator<mtp::default_set>::make();

	nk_buffer_init(&m_cmd_buf, &ui_alloc, m_cmd_capacity);
	nk_buffer_init(&m_vtx_buf, &ui_alloc, m_vtx_capacity);
	nk_buffer_init(&m_idx_buf, &ui_alloc, m_idx_capacity);
}


void UiPass::resize(const SurfaceInfo& surface_info)
{
	m_mtx_P_ortho = glm::ortho(
		0.0f,
		static_cast<float>(surface_info.width),
		static_cast<float>(surface_info.height),
		0.0f,
		-1.0f,
		1.0f
	);
}


void UiPass::set_program(Handle<Program> prog_ui)
{
	m_prog_ui = prog_ui;
}


void UiPass::execute(RenderQueue<UiDrawCommand>& queue, const SurfaceInfo& surface_info)
{
	const Program* program = m_hub.get<Program>(m_prog_ui);

	if (!program)
		return;
	if (!program->pipeline.id)
		return;

	sg_pass_action pass_action {};
	pass_action.colors[0].load_action  = SG_LOADACTION_LOAD;
	pass_action.colors[0].store_action = SG_STOREACTION_STORE;

	sg_pass pass {};
	pass.swapchain = sglue_swapchain();
	pass.action    = pass_action;

	sg_begin_pass(pass);

	for (const auto& draw_submission : queue.commands()) {
		if (!draw_submission.ctx) {
			continue;
		}

		const FontTexture* font_texture = m_hub.get<FontTexture>(draw_submission.font_texture);
		if (!font_texture || !font_texture->image.id || !font_texture->sampler.id) {
			continue;
		}

		m_ui_cfg.vertex_layout    = m_ui_layout;
		m_ui_cfg.vertex_size      = sizeof(UiVertex);
		m_ui_cfg.vertex_alignment = NK_ALIGNOF(UiVertex);
		m_ui_cfg.tex_null         = draw_submission.null_texture;

		nk_allocator ui_alloc = MetapoolNuklearAllocator<mtp::default_set>::make();

		bool grew = false;
		for (;;) {
			nk_buffer_clear(&m_cmd_buf);
			nk_buffer_clear(&m_vtx_buf);
			nk_buffer_clear(&m_idx_buf);

			int convert_result = nk_convert(draw_submission.ctx, &m_cmd_buf, &m_vtx_buf, &m_idx_buf, &m_ui_cfg);

			if ((convert_result & NK_CONVERT_COMMAND_BUFFER_FULL) ||
			    (convert_result & NK_CONVERT_VERTEX_BUFFER_FULL)  ||
			    (convert_result & NK_CONVERT_ELEMENT_BUFFER_FULL)) {
				m_cmd_capacity = m_cmd_buf.allocated ? m_cmd_buf.allocated * 2 : m_cmd_capacity * 2;
				m_vtx_capacity = m_vtx_buf.allocated ? m_vtx_buf.allocated * 2 : m_vtx_capacity * 2;
				m_idx_capacity = m_idx_buf.allocated ? m_idx_buf.allocated * 2 : m_idx_capacity * 2;

				nk_buffer_free(&m_cmd_buf);
				nk_buffer_free(&m_vtx_buf);
				nk_buffer_free(&m_idx_buf);

				nk_buffer_init(&m_cmd_buf, &ui_alloc, m_cmd_capacity);
				nk_buffer_init(&m_vtx_buf, &ui_alloc, m_vtx_capacity);
				nk_buffer_init(&m_idx_buf, &ui_alloc, m_idx_capacity);

				grew = true;
				continue;
			}
			break;
		}

		const size_t vtx_data_size = static_cast<size_t>(nk_buffer_total(&m_vtx_buf));
		const size_t idx_data_size = static_cast<size_t>(nk_buffer_total(&m_idx_buf));

		if (vtx_data_size == 0 || idx_data_size == 0) {
			nk_clear(draw_submission.ctx);
			continue;
		}

		if (grew) {
			sg_end_pass();

			sg_destroy_buffer(m_ui_vtx_buf);
			sg_buffer_desc vtx_buf {};
			vtx_buf.size = static_cast<size_t>(m_vtx_capacity);
			vtx_buf.usage.vertex_buffer  = true;
			vtx_buf.usage.dynamic_update = true;
			vtx_buf.label = "ui_vtx_buf";
			m_ui_vtx_buf = sg_make_buffer(&vtx_buf);

			sg_destroy_buffer(m_ui_idx_buf);
			sg_buffer_desc idx_buf {};
			idx_buf.size = static_cast<size_t>(m_idx_capacity);
			idx_buf.usage.index_buffer   = true;
			idx_buf.usage.dynamic_update = true;
			idx_buf.label = "ui_idx_buf";
			m_ui_idx_buf = sg_make_buffer(&idx_buf);

			sg_begin_pass(pass);
		}

		sg_range vtx_upload {
			.ptr  = m_vtx_buf.memory.ptr,
			.size = vtx_data_size
		};
		sg_range idx_upload {
			.ptr  = m_idx_buf.memory.ptr,
			.size = idx_data_size
		};

		sg_update_buffer(m_ui_vtx_buf, &vtx_upload);
		sg_update_buffer(m_ui_idx_buf, &idx_upload);

		sg_bindings bindings {};
		bindings.vertex_buffers[0] = m_ui_vtx_buf;
		bindings.index_buffer      = m_ui_idx_buf;
		bindings.images  [IMG_ui_u_font_tex] = font_texture->image;
		bindings.samplers[SMP_ui_u_font_smp] = font_texture->sampler;

		sg_apply_pipeline(program->pipeline);
		sg_apply_bindings(&bindings);
		sg_apply_uniforms(UB_ui_vs_params, SG_RANGE(m_mtx_P_ortho));

		const nk_draw_command* nuklear_command;
		const nk_draw_index* offset = static_cast<const nk_draw_index*>(nullptr);
		uint32_t last_tex = font_texture->image.id;

		nk_draw_foreach(nuklear_command, draw_submission.ctx, &m_cmd_buf) {
			if (!nuklear_command->elem_count)
				continue;

			int x = static_cast<int>(nuklear_command->clip_rect.x);
			int y = static_cast<int>(nuklear_command->clip_rect.y);
			int w = static_cast<int>(nuklear_command->clip_rect.w);
			int h = static_cast<int>(nuklear_command->clip_rect.h);

			if (x < 0) {w += x; x = 0;}
			if (y < 0) {h += y; y = 0;}
			if (x + w > static_cast<int>(surface_info.width))  w = static_cast<int>(surface_info.width)  - x;
			if (y + h > static_cast<int>(surface_info.height)) h = static_cast<int>(surface_info.height) - y;
			if (w < 0) w = 0;
			if (h < 0) h = 0;

			uint32_t cur_tex = static_cast<uint32_t>(nuklear_command->texture.id);
			if (cur_tex != last_tex) {
				bindings.images[IMG_ui_u_font_tex] = sg_image {cur_tex};
				sg_apply_bindings(&bindings);
				last_tex = cur_tex;
			}

			sg_apply_scissor_rect(x, y, w, h, true);
			sg_draw(static_cast<int>(offset - static_cast<const nk_draw_index*>(nullptr)), static_cast<int>(nuklear_command->elem_count), 1);
			offset += nuklear_command->elem_count;
		}

		nk_clear(draw_submission.ctx);
	}

	sg_end_pass();
}


void UiPass::release()
{
	if (m_ui_vtx_buf.id) {
		sg_destroy_buffer(m_ui_vtx_buf);
		m_ui_vtx_buf = {};
	}
	if (m_ui_idx_buf.id) {
		sg_destroy_buffer(m_ui_idx_buf);
		m_ui_idx_buf = {};
	}

	nk_buffer_free(&m_cmd_buf);
	nk_buffer_free(&m_vtx_buf);
	nk_buffer_free(&m_idx_buf);

	std::memset(&m_cmd_buf, 0, sizeof(m_cmd_buf));
	std::memset(&m_vtx_buf, 0, sizeof(m_vtx_buf));
	std::memset(&m_idx_buf, 0, sizeof(m_idx_buf));
}

} // hpr::rdr

