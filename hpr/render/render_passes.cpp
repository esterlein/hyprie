#include "render_passes.hpp"

#include "math.hpp"

#include "draw_queue_data.hpp"
#include "fx_data.hpp"
#include "render_context.hpp"
#include "storage_mass.hpp"
#include "ui_alloc.hpp"
#include "pixel_utils.hpp"
#include "vertex_mass.hpp"
#include "vertex_format.hpp"
#include "texture_format.hpp"
#include "storage_data.hpp"

#include "cue.glsl.h"
#include "cue_wire.glsl.h"
#include "grid.glsl.h"
#include "scene.glsl.h"
#include "scene_skinned.glsl.h"
#include "bitmap.glsl.h"
#include "overlay.glsl.h"
#include "overlay_wire.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_dilate.glsl.h"
#include "outline_blend.glsl.h"
#include "skybox.glsl.h"
#include "ibl_equirect.glsl.h"
#include "ibl_irradiance.glsl.h"
#include "ibl_prefilter.glsl.h"
#include "ibl_brdf.glsl.h"


namespace hpr::rdr {


log::PassStats ScenePass::execute(
	RenderQueue<SceneDrawCmd>&  scene_queue,
	RenderQueue<AnimDrawCmd>&   anim_queue,
	RenderQueue<ReplayDrawCmd>& replay_queue,
	const scn::SceneContext&    scene_ctx,
	const BindingContext&       binding_ctx,
	const StagingContext&       staging_ctx
)
{
	log::PassStats pass_stats {};

	/* sort and sync */

	scene_queue.sort();
	anim_queue.sort();
	replay_queue.clear();

	auto& scene_commands  = scene_queue.commands();
	auto& anim_commands   = anim_queue.commands();
	auto& scene_blob_mass = staging_ctx.scn_blob_mass;
	auto& anim_blob_mass  = staging_ctx.anm_blob_mass;

	for (auto& cmd : scene_commands) {

		SceneBlob blob = scene_blob_mass->get_raw(cmd.blob_idx);
		cmd.blob_idx   = scene_blob_mass->push_staged(blob);

		if (cmd.flags & static_cast<uint8_t>(SceneDrawCmdFlag::selected)) {
			replay_queue.push(ReplayDrawCmd {
				.sort_key =
					(static_cast<uint64_t>(cmd.mat_idx   & 0xFFFFFFU) << 40) |
					(static_cast<uint64_t>(cmd.vtx_base  & 0xFFFFU)   << 24) |
					(static_cast<uint64_t>(cmd.idx_first & 0xFFFFFFU)),
				.vtx_base  = cmd.vtx_base,
				.idx_first = cmd.idx_first,
				.idx_count = cmd.idx_count,
				.blob_idx  = cmd.blob_idx
			});
        }
	}

	for (auto& cmd : anim_commands) {
		AnimBlob blob = anim_blob_mass->get_raw(cmd.blob_idx);
		cmd.blob_idx  = anim_blob_mass->push_staged(blob);
	}

	scene_blob_mass->sync();
	anim_blob_mass->sync();

	/* set common scene pass state */

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

	sg_begin_pass(&pass);


	/* static scene pipeline */

	sg_apply_pipeline(binding_ctx.pipelines.scene_static.pipeline);

	sg_bindings bindings = {
		.vertex_buffers[0]           = binding_ctx.scn_vtx.vtx_buf,
		.index_buffer                = binding_ctx.scn_vtx.idx_buf,
		.views[VIEW_scene_ssbo_trs]  = binding_ctx.scn_blob_ssbo.view,
		.views[VIEW_scene_ssbo_mats] = binding_ctx.mat_inst_ssbo.view,

		.views[VIEW_scene_u_irradiance_cube] = binding_ctx.environment.irr_view,
		.views[VIEW_scene_u_prefilter_cube]  = binding_ctx.environment.pref_view,
		.views[VIEW_scene_u_brdf_lut]        = binding_ctx.environment.brdf_view
	};

	for (uint32_t i = 0; i < binding_ctx.texarrays.count; ++i) {
		bindings.views[VIEW_scene_u_tex_arr_2048_srgb + i] =
			binding_ctx.texarrays.views[i];
	}

	bindings.samplers[SMP_scene_u_smp_linrep] =
		binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_repeat)];

	bindings.samplers[SMP_scene_u_smp_linclamp] =
		binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_clamp)];

	sg_apply_bindings(&bindings);

	scene_u_cam_vs_t u_cam_vs {};

	std::memcpy(
		u_cam_vs.mtx_V,
		glm::value_ptr(scene_ctx.draw_view.mtx_V),
		sizeof(u_cam_vs.mtx_V)
	);
	std::memcpy(
		u_cam_vs.mtx_VP,
		glm::value_ptr(scene_ctx.draw_view.mtx_VP),
		sizeof(u_cam_vs.mtx_VP)
	);
	std::memcpy(
		u_cam_vs.cam_pos_W,
		glm::value_ptr(scene_ctx.draw_view.pos_W),
		sizeof(u_cam_vs.cam_pos_W)
	);

	scene_u_cam_fs_t u_cam_fs {};

	std::memcpy(
		u_cam_fs.mtx_V,
		glm::value_ptr(scene_ctx.draw_view.mtx_V),
		sizeof(u_cam_fs.mtx_V)
	);
	std::memcpy(
		u_cam_fs.mtx_VP,
		glm::value_ptr(scene_ctx.draw_view.mtx_VP),
		sizeof(u_cam_fs.mtx_VP)
	);
	std::memcpy(
		u_cam_fs.cam_pos_W,
		glm::value_ptr(scene_ctx.draw_view.pos_W),
		sizeof(u_cam_fs.cam_pos_W)
	);

	scene_u_light_t u_light {};
	
	const uint32_t light_count = (scene_ctx.light_set.count > scn::cfg::max_light_count)
		? scn::cfg::max_light_count
		: scene_ctx.light_set.count;

	u_light.light_count = static_cast<int>(light_count);

	std::memcpy(
		u_light.ambient_rgb,
		&scene_ctx.light_set.ambient_rgb[0],
		sizeof(u_light.ambient_rgb)
	);

	for (uint32_t i = 0; i < light_count; ++i) {
		const auto& light_item = scene_ctx.light_set.items[i];

		u_light.scalar_params[i][0] = static_cast<float>(light_item.type);
		u_light.scalar_params[i][1] = light_item.intensity;
		u_light.scalar_params[i][2] = light_item.range;

		std::memcpy(
			u_light.color_rgb[i],
			&light_item.color_rgb[0],
			sizeof(u_light.color_rgb[i])
		);
		std::memcpy(
			u_light.dir_world[i],
			&light_item.dir_world[0],
			sizeof(u_light.dir_world[i])
		);
		std::memcpy(
			u_light.pos_world[i],
			&light_item.pos_world[0],
			sizeof(u_light.pos_world[i])
		);

		u_light.spot_params[i][0] = light_item.cos_inner;
		u_light.spot_params[i][1] = light_item.cos_outer;
	}

	sg_apply_uniforms(UB_scene_u_cam_vs, SG_RANGE(u_cam_vs));
	sg_apply_uniforms(UB_scene_u_cam_fs, SG_RANGE(u_cam_fs));
	sg_apply_uniforms(UB_scene_u_light,  SG_RANGE(u_light));

	for (uint32_t cmd_idx = 0; cmd_idx < scene_commands.size(); ) {

		const auto& cmd_start = scene_commands[cmd_idx];
		uint32_t instance_num = 1;

		while (cmd_idx + instance_num < scene_commands.size()) {
			const auto& cmd_next = scene_commands[cmd_idx + instance_num];

			if (cmd_next.vtx_base  == cmd_start.vtx_base  &&
				cmd_next.idx_first == cmd_start.idx_first &&
				cmd_next.idx_count == cmd_start.idx_count &&
				cmd_next.mat_idx   == cmd_start.mat_idx) {
				instance_num++;
			}
			else {
				break;
			}
		}

		scene_u_inst_t inst {};
		inst.base_inst_idx = static_cast<int>(cmd_start.blob_idx);

		sg_apply_uniforms(UB_scene_u_inst, SG_RANGE(inst));

		sg_draw_ex(
			static_cast<int>(cmd_start.idx_first),
			static_cast<int>(cmd_start.idx_count),
			static_cast<int>(instance_num),
			static_cast<int>(cmd_start.vtx_base),
			static_cast<int>(cmd_start.blob_idx)
		);

		pass_stats.draw_calls++;
		pass_stats.submeshes += instance_num;
		pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
		pass_stats.triangles += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num) / 3ULL;

		cmd_idx += instance_num;
	}


	/* skinned scene pipeline */

	sg_apply_pipeline(binding_ctx.pipelines.scene_skinned.pipeline);

	sg_bindings anim_bindings = {
		.vertex_buffers[0]                    = binding_ctx.anm_vtx.vtx_buf,
		.index_buffer                         = binding_ctx.anm_vtx.idx_buf,
		.views[VIEW_scene_skinned_ssbo_trs]   = binding_ctx.anm_blob_ssbo.view,
		.views[VIEW_scene_skinned_ssbo_mats]  = binding_ctx.mat_inst_ssbo.view,
		.views[VIEW_scene_skinned_ssbo_bones] = binding_ctx.anm_bones_ssbo.view
	};

	for (uint32_t i = 0; i < binding_ctx.texarrays.count; ++i) {
		anim_bindings.views[VIEW_scene_u_tex_arr_2048_srgb + i] = binding_ctx.texarrays.views[i];
	}

	anim_bindings.samplers[SMP_scene_u_smp_linrep] =
		binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_repeat)];

	sg_apply_bindings(&anim_bindings);

	sg_apply_uniforms(UB_scene_u_cam_vs, SG_RANGE(u_cam_vs));
	sg_apply_uniforms(UB_scene_u_cam_fs, SG_RANGE(u_cam_fs));
	sg_apply_uniforms(UB_scene_u_light,  SG_RANGE(u_light));

	for (uint32_t cmd_idx = 0; cmd_idx < anim_commands.size(); ) {

		const auto& cmd_start = anim_commands[cmd_idx];
		uint32_t instance_num = 1;

		while (cmd_idx + instance_num < anim_commands.size()) {
			const auto& cmd_next = anim_commands[cmd_idx + instance_num];

			if (cmd_next.vtx_base  == cmd_start.vtx_base  &&
				cmd_next.idx_first == cmd_start.idx_first &&
				cmd_next.idx_count == cmd_start.idx_count) {
				instance_num++;
			}
			else {
				break;
			}
		}

		scene_u_inst_t inst {};
		inst.base_inst_idx = static_cast<int>(cmd_start.blob_idx);

		sg_apply_uniforms(UB_scene_u_inst, SG_RANGE(inst));

		sg_draw_ex(
			static_cast<int>(cmd_start.idx_first),
			static_cast<int>(cmd_start.idx_count),
			static_cast<int>(instance_num),
			static_cast<int>(cmd_start.vtx_base),
			static_cast<int>(cmd_start.blob_idx)
		);

		pass_stats.draw_calls++;
		pass_stats.submeshes += instance_num;
		pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
		pass_stats.triangles += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num) / 3ULL;

		cmd_idx += instance_num;
	}

	/* skybox pipeline */

	sg_apply_pipeline(binding_ctx.pipelines.skybox.pipeline);

	sg_bindings skybox_bindings {};
	skybox_bindings.vertex_buffers[0]                 = binding_ctx.gen_vtx.vtx_buf;
	skybox_bindings.index_buffer                      = binding_ctx.gen_vtx.idx_buf;
	skybox_bindings.views[VIEW_skybox_u_skybox_cube]  = binding_ctx.environment.env_view;
	skybox_bindings.samplers[SMP_skybox_u_smp_linear] = binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_clamp)];

	sg_apply_bindings(&skybox_bindings);

	skybox_u_camera_t skybox_cam {};
	glm::mat4 view_rot_only = glm::mat4(glm::mat3(scene_ctx.draw_view.mtx_V));
	glm::mat4 vp_skybox = scene_ctx.draw_view.mtx_P * view_rot_only;
	std::memcpy(skybox_cam.mtx_VP, glm::value_ptr(vp_skybox), sizeof(skybox_cam.mtx_VP));

	sg_apply_uniforms(UB_skybox_u_camera, SG_RANGE(skybox_cam));

	uint32_t box_idx_first =
		m_canonical_shapes.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::Box)].idx_first;
	uint32_t box_idx_count =
		m_canonical_shapes.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::Box)].idx_count;
	uint32_t box_vtx_base  =
		m_canonical_shapes.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::Box)].vtx_base;

	sg_draw_ex(
		static_cast<int>(box_idx_first),
		static_cast<int>(box_idx_count),
		1,
		0,
		0
	);

	pass_stats.draw_calls++;
	pass_stats.indices   += box_idx_count;
	pass_stats.triangles += box_idx_count / 3ULL;

	sg_end_pass();

	return pass_stats;
	sg_end_pass();

	return pass_stats;
}


log::PassStats OutlinePass::execute(
	RenderQueue<ReplayDrawCmd>& replay_queue,
	const scn::SceneContext&    scene_ctx,
	const BindingContext&       binding_ctx,
	const SurfaceInfo&          surface_info
)
{
	log::PassStats pass_stats {};

	if (replay_queue.empty())
		return pass_stats;

	const float w = static_cast<float>(surface_info.width);
	const float h = static_cast<float>(surface_info.height);

	/* mask pass */
	{
		sg_pass_action pass_action {};
		pass_action.colors[0].load_action  = SG_LOADACTION_CLEAR;
		pass_action.colors[0].store_action = SG_STOREACTION_STORE;
		pass_action.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 0.0f};

		sg_attachments attachments {};
		attachments.colors[0] = binding_ctx.targets.mask_draw_view;

		sg_pass pass {};
		pass.attachments = attachments;
		pass.action      = pass_action;

		sg_begin_pass(&pass);
		sg_apply_pipeline(binding_ctx.pipelines.outline_mask.pipeline);

		sg_bindings bindings {};
		bindings.vertex_buffers[0]                 = binding_ctx.scn_vtx.vtx_buf;
		bindings.index_buffer                      = binding_ctx.scn_vtx.idx_buf;
		bindings.views[VIEW_outline_mask_ssbo_trs] = binding_ctx.scn_blob_ssbo.view;

		sg_apply_bindings(&bindings);

		outline_mask_u_camera_t u_camera {};

		std::memcpy(
			u_camera.mtx_V,
			glm::value_ptr(scene_ctx.draw_view.mtx_V),
			sizeof(u_camera.mtx_V)
		);
		std::memcpy(
			u_camera.mtx_VP,
			glm::value_ptr(scene_ctx.draw_view.mtx_VP),
			sizeof(u_camera.mtx_VP)
		);

		sg_apply_uniforms(UB_outline_mask_u_camera, SG_RANGE(u_camera));

		replay_queue.sort();
		auto& commands = replay_queue.commands();

		for (uint32_t cmd_idx = 0; cmd_idx < commands.size(); ++cmd_idx) {
			const auto& cmd = commands[cmd_idx];

			outline_mask_u_inst_t inst {};
			inst.base_inst_idx = static_cast<int>(cmd.blob_idx);

			sg_apply_uniforms(UB_outline_mask_u_inst, SG_RANGE(inst));

			sg_draw_ex(
				static_cast<int>(cmd.idx_first),
				static_cast<int>(cmd.idx_count),
				1,
				static_cast<int>(cmd.vtx_base),
				0
			);

			++pass_stats.draw_calls;
			++pass_stats.submeshes;

			pass_stats.indices   += static_cast<uint64_t>(cmd.idx_count);
			pass_stats.triangles += static_cast<uint64_t>(cmd.idx_count) / 3ULL;
		}

		sg_end_pass();
	}

	/* dilate pass */
	{
		sg_pass_action pass_action {};
		pass_action.colors[0].load_action  = SG_LOADACTION_CLEAR;
		pass_action.colors[0].store_action = SG_STOREACTION_STORE;
		pass_action.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 0.0f};

		sg_attachments attachments {};
		attachments.colors[0] = binding_ctx.targets.dilate_draw_view;

		sg_pass pass {};
		pass.attachments = attachments;
		pass.action      = pass_action;

		sg_begin_pass(&pass);

		sg_bindings bindings {};
		bindings.views[VIEW_outline_dilate_u_mask_tex] = binding_ctx.targets.mask_smpl_view;
		bindings.samplers[SMP_outline_dilate_u_mask_smp] =
			binding_ctx.samplers.types[static_cast<size_t>(SamplerType::nearest_clamp)];

		sg_apply_pipeline(binding_ctx.pipelines.outline_dilate.pipeline);
		sg_apply_bindings(&bindings);

		outline_dilate_fs_params_t outline_dilate_params {};
		outline_dilate_params.mask_tex_size_px[0] = w;
		outline_dilate_params.mask_tex_size_px[1] = h;
		outline_dilate_params.radius_px           = static_cast<float>(m_radius_px);

		sg_apply_uniforms(UB_outline_dilate_fs_params, SG_RANGE(outline_dilate_params));

		pass_stats.draw_calls++;
		pass_stats.triangles += 1;

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

		sg_begin_pass(&pass);

		sg_apply_viewport(0, 0, surface_info.width, surface_info.height, true);

		sg_bindings bindings {};
		auto smp_lin = binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_clamp)];

		bindings.views[VIEW_outline_blend_u_mask_orig_tex]      = binding_ctx.targets.mask_smpl_view;
		bindings.samplers[SMP_outline_blend_u_mask_orig_smp]    = smp_lin;
		bindings.views[VIEW_outline_blend_u_mask_dilated_tex]   = binding_ctx.targets.dilate_smpl_view;
		bindings.samplers[SMP_outline_blend_u_mask_dilated_smp] = smp_lin;

		sg_apply_pipeline(binding_ctx.pipelines.outline_blend.pipeline);
		sg_apply_bindings(&bindings);

		outline_blend_fs_params_t outline_blend_params {};
		outline_blend_params.mask_tex_size_px[0] = w;
		outline_blend_params.mask_tex_size_px[1] = h;

		frgb_from_u32(m_rgb888, outline_blend_params.outline_color_rgb);

		const float outline_alpha = static_cast<float>(m_alpha & 0xFF) / 255.0f;
		outline_blend_params.outline_alpha = outline_alpha;

		sg_apply_uniforms(UB_outline_blend_fs_params, SG_RANGE(outline_blend_params));

		pass_stats.draw_calls++;
		pass_stats.triangles += 1;

		sg_draw(0, 3, 1);

		sg_end_pass();
	}

	return pass_stats;
}


void OutlinePass::set_outline_params(uint32_t rgb888, uint32_t alpha, int radius_px)
{
	m_rgb888    = rgb888;
	m_alpha     = alpha;
	m_radius_px = radius_px;
}


log::PassStats CompositorPass::execute(
	RenderQueue<FxDrawCmd>&      fx_queue,
	RenderQueue<CueDrawCmd>&     cue_queue,
	RenderQueue<OverlayDrawCmd>& overlay_queue,
	const scn::SceneContext&     scene_ctx,
	const BindingContext&        binding_ctx,
	StagingContext&              staging_ctx,
	const SurfaceInfo&           surface_info
)
{
	log::PassStats pass_stats {};

	fx_queue.sort();
	cue_queue.sort();
	overlay_queue.sort();

	auto& cue_cmds = cue_queue.commands();
	if (!cue_cmds.empty()) {
		auto& cue_trs_mass = staging_ctx.cue_blob_mass;

		for (auto& cmd : cue_cmds) {
			rdr::CueBlob cue_trs = cue_trs_mass->get_raw(cmd.blob_idx);
			cmd.blob_idx         = cue_trs_mass->push_staged(cue_trs);
		}

		cue_trs_mass->sync();
	}

	auto& overlay_cmds = overlay_queue.commands();
	if (!overlay_cmds.empty()) {
		auto& overlay_trs_mass = staging_ctx.orl_blob_mass;

		for (auto& cmd : overlay_cmds) {
			rdr::OverlayBlob trs = overlay_trs_mass->get_raw(cmd.blob_idx);
			cmd.blob_idx         = overlay_trs_mass->push_staged(trs);
		}

		overlay_trs_mass->sync();
	}

	sg_pass_action pass_action {};
	pass_action.colors[0].load_action = SG_LOADACTION_LOAD;
	pass_action.depth.load_action     = SG_LOADACTION_LOAD;

	sg_pass pass {};
	pass.swapchain = sglue_swapchain();
	pass.action    = pass_action;

	sg_begin_pass(pass);

	/* fx - grid */

	if (!fx_queue.commands().empty()) {

		sg_apply_pipeline(binding_ctx.pipelines.grid.pipeline);
		
		sg_apply_viewport(
			0,
			0,
			static_cast<int>(surface_info.width),
			static_cast<int>(surface_info.height),
			true
		);

		const glm::mat4 mtx_VP_inv = glm::inverse(scene_ctx.draw_view.mtx_VP);

		grid_u_camera_t camera {};
		std::memcpy(
			camera.mtx_VP,
			glm::value_ptr(scene_ctx.draw_view.mtx_VP),
			sizeof(camera.mtx_VP)
		);
		std::memcpy(
			camera.mtx_VP_inv,
			glm::value_ptr(mtx_VP_inv),
			sizeof(camera.mtx_VP_inv)
		);
		camera.fb_size_px[0] = static_cast<float>(surface_info.width);
		camera.fb_size_px[1] = static_cast<float>(surface_info.height);

		sg_apply_uniforms(UB_grid_u_camera, SG_RANGE(camera));

		for (const auto& fx_cmd : fx_queue.commands()) {
			const auto* grid_pack = reinterpret_cast<const GridPack*>(fx_cmd.payload.data());

			grid_u_fx_payload_t payload {};

			std::memcpy(
				payload.minor_rgba,
				glm::value_ptr(grid_pack->minor_rgba),
				sizeof(glm::vec4)
			);
			std::memcpy(
				payload.major_rgba,
				glm::value_ptr(grid_pack->major_rgba),
				sizeof(glm::vec4)
			);
			std::memcpy(
				payload.minor_vis_range_px,
				glm::value_ptr(grid_pack->minor_vis_range_px),
				sizeof(glm::vec2)
			);
			std::memcpy(
				payload.major_vis_range_px,
				glm::value_ptr(grid_pack->major_vis_range_px),
				sizeof(glm::vec2)
			);
			
			payload.line_width_px    = grid_pack->line_width_px;
			payload.cell_size        = grid_pack->cell_size;
			payload.y_plane          = grid_pack->y_plane;
			payload.major_step_cells = grid_pack->major_step_cells;

			double cam_x      = static_cast<double>(scene_ctx.draw_view.pos_W.x);
			double cam_z      = static_cast<double>(scene_ctx.draw_view.pos_W.z);
			double major_size = static_cast<double>(grid_pack->cell_size * grid_pack->major_step_cells);

			payload.cam_offset_xz[0]   = static_cast<float>(std::fmod(cam_x, major_size));
			payload.cam_offset_xz[1]   = static_cast<float>(std::fmod(cam_z, major_size));
			payload.camera_world_xz[0] = scene_ctx.draw_view.pos_W.x;
			payload.camera_world_xz[1] = scene_ctx.draw_view.pos_W.z;
			payload.camera_y           = scene_ctx.draw_view.pos_W.y;

			sg_apply_uniforms(UB_grid_u_fx_payload, SG_RANGE(payload));

			pass_stats.draw_calls++;
			pass_stats.triangles += 1;

			sg_draw(0, 3, 1);
		}
	}

	/* cues */

	if (!cue_cmds.empty()) {

		sg_apply_viewport(
			0,
			0,
			static_cast<int>(surface_info.width),
			static_cast<int>(surface_info.height),
			true
		);

		for (uint32_t cmd_idx = 0; cmd_idx < cue_cmds.size(); ) {

			const auto& cmd_start = cue_cmds[cmd_idx];
			uint32_t instance_num = 1;

			while (cmd_idx + instance_num < cue_cmds.size()) {
				const auto& cmd_next = cue_cmds[cmd_idx + instance_num];

				if (cmd_next.vtx_base      == cmd_start.vtx_base      &&
					cmd_next.idx_first     == cmd_start.idx_first     &&
					cmd_next.idx_count     == cmd_start.idx_count     &&
					cmd_next.cue_mask      == cmd_start.cue_mask      &&
					cmd_next.palette_slice == cmd_start.palette_slice &&
					cmd_next.tilemap_slice == cmd_start.tilemap_slice) {
					instance_num++;
				}
				else {
					break;
				}
			}

			bool is_wire = (cmd_start.cue_mask == 0U);

			if (is_wire) {

				sg_apply_pipeline(binding_ctx.pipelines.cue_wire.pipeline);

				sg_bindings bindings {};
				bindings.views[VIEW_cue_wire_ssbo_trs] = binding_ctx.cue_blob_ssbo.view;
				bindings.views[VIEW_cue_wire_ssbo_vtx] = binding_ctx.vtx_gen_ssbo.view;
				bindings.views[VIEW_cue_wire_ssbo_idx] = binding_ctx.idx_gen_ssbo.view;

				bindings.views[VIEW_cue_wire_u_tex_palette]   = binding_ctx.palettes.view;
				bindings.samplers[SMP_cue_wire_u_smp_palette] =
					binding_ctx.samplers.types[static_cast<size_t>(SamplerType::nearest_clamp)];

				sg_apply_bindings(&bindings);

				cue_wire_u_camera_t u_camera {};

				std::memcpy(
					u_camera.mtx_VP,
					glm::value_ptr(scene_ctx.draw_view.mtx_VP),
					sizeof(u_camera.mtx_VP)
				);

				u_camera.viewport_px[0] = static_cast<float>(surface_info.width);
				u_camera.viewport_px[1] = static_cast<float>(surface_info.height);

				sg_apply_uniforms(UB_cue_wire_u_camera, SG_RANGE(u_camera));

				cue_wire_u_cmd_t u_cmd {};
				u_cmd.vtx_base           = static_cast<int32_t>(cmd_start.vtx_base);
				u_cmd.idx_first          = static_cast<int32_t>(cmd_start.idx_first);
				u_cmd.edges_per_instance = cmd_start.idx_count / 2;
				u_cmd.base_trs_idx       = static_cast<int32_t>(cmd_start.blob_idx);

				sg_apply_uniforms(UB_cue_wire_u_cmd, SG_RANGE(u_cmd));

				cue_wire_u_cue_params_t cue_params {};
				cue_params.mask    = static_cast<int32_t>(cmd_start.cue_mask);
				cue_params.palette = static_cast<int32_t>(cmd_start.palette_slice);
				cue_params.tilemap = static_cast<int32_t>(cmd_start.tilemap_slice);

				sg_apply_uniforms(UB_cue_wire_u_cue_params, SG_RANGE(cue_params));

				sg_draw(
					0,
					6,
					static_cast<int32_t>(u_cmd.edges_per_instance * instance_num)
				);

				pass_stats.draw_calls++;
				pass_stats.submeshes += instance_num;
				pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
			}
			else {

				sg_apply_pipeline(binding_ctx.pipelines.cue.pipeline);

				sg_bindings bindings {};
				bindings.vertex_buffers[0]               = binding_ctx.gen_vtx.vtx_buf;
				bindings.index_buffer                    = binding_ctx.gen_vtx.idx_buf;
				bindings.views[VIEW_cue_ssbo_trs]        = binding_ctx.cue_blob_ssbo.view;
				bindings.views[VIEW_cue_u_tex_palette]   = binding_ctx.palettes.view;

				bindings.samplers[SMP_cue_u_smp_palette] =
					binding_ctx.samplers.types[static_cast<size_t>(SamplerType::nearest_clamp)];

				bindings.views[VIEW_cue_u_tex_tilemap]   = binding_ctx.tilemaps.views[0];
				bindings.samplers[SMP_cue_u_smp_tilemap] =
					binding_ctx.samplers.types[static_cast<size_t>(SamplerType::nearest_clamp)];

				sg_apply_bindings(&bindings);

				cue_u_camera_t u_camera {};
				std::memcpy(
					u_camera.mtx_VP,
					glm::value_ptr(scene_ctx.draw_view.mtx_VP),
					sizeof(u_camera.mtx_VP)
				);

				sg_apply_uniforms(UB_cue_u_camera, SG_RANGE(u_camera));

				cue_u_cue_params_t cue_params {};
				cue_params.mask    = static_cast<int32_t>(cmd_start.cue_mask);
				cue_params.palette = static_cast<int32_t>(cmd_start.palette_slice);
				cue_params.tilemap = static_cast<int32_t>(cmd_start.tilemap_slice);

				sg_apply_uniforms(UB_cue_u_cue_params, SG_RANGE(cue_params));

				sg_draw_ex(
					static_cast<int>(cmd_start.idx_first),
					static_cast<int>(cmd_start.idx_count),
					static_cast<int>(instance_num),
					static_cast<int>(cmd_start.vtx_base),
					static_cast<int>(cmd_start.blob_idx)
				);

				pass_stats.draw_calls++;
				pass_stats.submeshes += instance_num;
				pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
				pass_stats.triangles += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num) / 3ULL;
			}

			cmd_idx += instance_num;
		}
	}

	/* overlays */

	if (!overlay_cmds.empty()) {

		sg_apply_viewport(
			0,
			0,
			static_cast<int>(surface_info.width),
			static_cast<int>(surface_info.height),
			true
		);

		for (uint32_t cmd_idx = 0; cmd_idx < overlay_cmds.size(); ) {

			const auto& cmd_start = overlay_cmds[cmd_idx];
			uint32_t instance_num = 1;

			while (cmd_idx + instance_num < overlay_cmds.size()) {

				const auto& cmd_next = overlay_cmds[cmd_idx + instance_num];

				if (cmd_next.vtx_base  == cmd_start.vtx_base  &&
					cmd_next.idx_first == cmd_start.idx_first &&
					cmd_next.idx_count == cmd_start.idx_count &&
					cmd_next.flags     == cmd_start.flags) {
					instance_num++;
				}
				else {
					break;
				}
			}

			bool is_wire = (cmd_start.flags == 1U);

			if (is_wire) {

				sg_apply_pipeline(binding_ctx.pipelines.overlay_wire.pipeline);

				sg_bindings bindings {};
				bindings.views[VIEW_overlay_wire_ssbo_trs] = binding_ctx.orl_blob_ssbo.view;
				bindings.views[VIEW_overlay_wire_ssbo_vtx] = binding_ctx.vtx_gen_ssbo.view;
				bindings.views[VIEW_overlay_wire_ssbo_idx] = binding_ctx.idx_gen_ssbo.view;
				
				sg_apply_bindings(&bindings);

				overlay_wire_u_camera_t u_camera {};
				std::memcpy(
					u_camera.mtx_VP,
					glm::value_ptr(scene_ctx.draw_view.mtx_VP),
					sizeof(u_camera.mtx_VP)
				);

				u_camera.viewport_px[0] = static_cast<float>(surface_info.width);
				u_camera.viewport_px[1] = static_cast<float>(surface_info.height);

				sg_apply_uniforms(UB_overlay_wire_u_camera, SG_RANGE(u_camera));

				overlay_wire_u_cmd_t u_cmd {};
				u_cmd.vtx_base           = static_cast<int32_t>(cmd_start.vtx_base);
				u_cmd.idx_first          = static_cast<int32_t>(cmd_start.idx_first);
				u_cmd.edges_per_instance = cmd_start.idx_count / 2;
				u_cmd.base_trs_idx       = static_cast<int32_t>(cmd_start.blob_idx);

				sg_apply_uniforms(UB_overlay_wire_u_cmd, SG_RANGE(u_cmd));

				sg_draw(
					0,
					6,
					static_cast<int32_t>(u_cmd.edges_per_instance * instance_num)
				);

				pass_stats.draw_calls++;
				pass_stats.submeshes += instance_num;
				pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
			}
			else {

				sg_apply_pipeline(binding_ctx.pipelines.overlay.pipeline);

				sg_bindings bindings {};
				bindings.vertex_buffers[0]            = binding_ctx.gen_vtx.vtx_buf;
				bindings.index_buffer                 = binding_ctx.gen_vtx.idx_buf;
				bindings.views[VIEW_overlay_ssbo_trs] = binding_ctx.orl_blob_ssbo.view;
				
				sg_apply_bindings(&bindings);

				overlay_u_camera_t u_camera {};
				std::memcpy(
					u_camera.mtx_VP,
					glm::value_ptr(scene_ctx.draw_view.mtx_VP),
					sizeof(u_camera.mtx_VP)
				);

				sg_apply_uniforms(UB_overlay_u_camera, SG_RANGE(u_camera));

				overlay_u_instance_t u_instance {};
				u_instance.base_instance = static_cast<int>(cmd_start.blob_idx);
				
				sg_apply_uniforms(UB_overlay_u_instance, SG_RANGE(u_instance));

				sg_draw_ex(
					static_cast<int>(cmd_start.idx_first),
					static_cast<int>(cmd_start.idx_count),
					static_cast<int>(instance_num),
					static_cast<int>(cmd_start.vtx_base),
					static_cast<int>(cmd_start.blob_idx)
				);

				pass_stats.draw_calls++;
				pass_stats.submeshes += instance_num;
				pass_stats.indices   += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num);
				pass_stats.triangles += (static_cast<uint64_t>(cmd_start.idx_count) * instance_num) / 3ULL;
			}

			cmd_idx += instance_num;
		}
	}

	sg_end_pass();
	
	return pass_stats;
}


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

	m_ui_layout[0] = {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(BitmapVertex, pos)};
	m_ui_layout[1] = {NK_VERTEX_TEXCOORD, NK_FORMAT_USHORT,   NK_OFFSETOF(BitmapVertex, uv)};
	m_ui_layout[2] = {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(BitmapVertex, rgb)};

	m_ui_layout[3].attribute = NK_VERTEX_ATTRIBUTE_COUNT;
	m_ui_layout[3].format    = NK_FORMAT_COUNT;
	m_ui_layout[3].offset    = 0;

	m_ui_cfg.vertex_layout        = m_ui_layout;
	m_ui_cfg.vertex_size          = sizeof(BitmapVertex);
	m_ui_cfg.vertex_alignment     = NK_ALIGNOF(BitmapVertex);
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


log::PassStats UiPass::execute(
	RenderQueue<UiDrawCmd>& queue,
	const SurfaceInfo&      surface_info,
	const BindingContext&   rdr_ctx
)
{
	log::PassStats pass_stats {};

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

		m_ui_cfg.vertex_layout    = m_ui_layout;
		m_ui_cfg.vertex_size      = sizeof(BitmapVertex);
		m_ui_cfg.vertex_alignment = NK_ALIGNOF(BitmapVertex);
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
			m_ui_vtx_buf  = sg_make_buffer(&vtx_buf);

			sg_destroy_buffer(m_ui_idx_buf);
			sg_buffer_desc idx_buf {};
			idx_buf.size = static_cast<size_t>(m_idx_capacity);
			idx_buf.usage.index_buffer   = true;
			idx_buf.usage.dynamic_update = true;
			idx_buf.label = "ui_idx_buf";
			m_ui_idx_buf  = sg_make_buffer(&idx_buf);

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
		bindings.vertex_buffers[0]        = m_ui_vtx_buf;
		bindings.index_buffer             = m_ui_idx_buf;
		bindings.vertex_buffer_offsets[0] = 0;
		bindings.index_buffer_offset      = 0;
		bindings.views[VIEW_bitmap_u_font_tex]   = rdr_ctx.nk_atlas.view;
		bindings.samplers[SMP_bitmap_u_font_smp] = rdr_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_clamp)];

		sg_apply_pipeline(rdr_ctx.pipelines.bitmap.pipeline);
		sg_apply_bindings(&bindings);
		sg_apply_uniforms(UB_bitmap_vs_params, SG_RANGE(m_mtx_P_ortho));

		const nk_draw_command* nuklear_command;
		const nk_draw_index* offset = static_cast<const nk_draw_index*>(nullptr);
		uint32_t last_tex = rdr_ctx.nk_atlas.view.id;

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
				bindings.views[VIEW_bitmap_u_font_tex] = sg_view {cur_tex};
				sg_apply_bindings(&bindings);
				last_tex = cur_tex;
			}

			sg_apply_scissor_rect(x, y, w, h, true);

			pass_stats.draw_calls++;
			pass_stats.indices   += static_cast<uint64_t>(nuklear_command->elem_count);
			pass_stats.triangles += static_cast<uint64_t>(nuklear_command->elem_count) / 3ULL;

			sg_draw(
				static_cast<int>(offset - static_cast<const nk_draw_index*>(nullptr)),
				static_cast<int>(nuklear_command->elem_count),
				1
			);

			offset += nuklear_command->elem_count;
		}

		nk_clear(draw_submission.ctx);
	}

	sg_end_pass();

	return pass_stats;
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


log::PassStats DebugPass::execute(
	RenderQueue<DebugDrawCmd>& queue,
	const BindingContext&      binding_ctx,
	StagingContext&            staging_ctx,
	const SurfaceInfo&         surface_info
)
{
	log::PassStats pass_stats {};
	if (queue.commands().empty())
		return pass_stats;

	queue.sort();

	sg_pass_action pass_action {};
	pass_action.colors[0].load_action  = SG_LOADACTION_LOAD;
	pass_action.colors[0].store_action = SG_STOREACTION_STORE;

	sg_pass pass {};
	pass.swapchain = sglue_swapchain();
	pass.action    = pass_action;

	sg_begin_pass(pass);

	sg_apply_viewport(0, 0, surface_info.width, surface_info.height, true);
	sg_apply_pipeline(binding_ctx.pipelines.bitmap.pipeline);
	sg_apply_uniforms(UB_bitmap_vs_params, SG_RANGE(m_mtx_P_ortho));

	sg_bindings bindings {};
	bindings.vertex_buffers[0] = binding_ctx.btm_vtx.vtx_buf;
	bindings.index_buffer	   = binding_ctx.btm_vtx.idx_buf;
	bindings.samplers[SMP_bitmap_u_font_smp] = 
        binding_ctx.samplers.types[static_cast<size_t>(SamplerType::nearest_clamp)];

	Handle<Font> hnd_curr_font;
	const Font* curr_font = nullptr;
	auto& vtx_mass = *staging_ctx.btm_vtx_mass;

	auto flush = [&](const Font* font) {
		if (vtx_mass.vtx_count() == 0) return;
		vtx_mass.sync();
		bindings.views[VIEW_bitmap_u_font_tex] = binding_ctx.fonts.views[font->atlas_idx];
		sg_apply_bindings(&bindings);
		sg_draw(0, static_cast<int>(vtx_mass.idx_count()), 1);
		pass_stats.draw_calls++;
		pass_stats.indices   += vtx_mass.idx_count();
		pass_stats.triangles += vtx_mass.idx_count() / 3ULL;
		vtx_mass.clear();
	};

	for (const auto& cmd : queue.commands()) {
		if (cmd.font != hnd_curr_font) {
			if (curr_font) flush(curr_font);
			hnd_curr_font = cmd.font;
			curr_font     = m_hub.get<Font>(hnd_curr_font);
		}

		for (const DebugTextLine& line : cmd.lines) {
			int32_t pen_x = line.cell_x;
			int32_t pen_y = line.cell_y;

			for (size_t j = 0; j < line.text.size() && line.text[j] != '\0'; ++j) {
				const uint8_t ch = static_cast<uint8_t>(line.text[j]);
				const int32_t glyph_idx = static_cast<int32_t>(ch) - static_cast<int32_t>(curr_font->metrics.first_char);
				
				if (glyph_idx < 0 || glyph_idx >= static_cast<int32_t>(curr_font->metrics.num_chars)) continue;
				const FontGlyph& glyph = curr_font->metrics.glyphs[static_cast<size_t>(glyph_idx)];

				if ((glyph.x_min_px == glyph.x_max_px) || (glyph.y_min_px == glyph.y_max_px)) {
					pen_x += glyph.advance_px; continue;
				}

				uint16_t base = static_cast<uint16_t>(vtx_mass.vtx_count());

				BitmapVertex vertices[4] = {
					{v2f32(pen_x + glyph.x_min_px, pen_y + glyph.y_min_px), v2u16{glyph.u_min, glyph.v_min}, line.color},
					{v2f32(pen_x + glyph.x_max_px, pen_y + glyph.y_min_px), v2u16{glyph.u_max, glyph.v_min}, line.color},
					{v2f32(pen_x + glyph.x_max_px, pen_y + glyph.y_max_px), v2u16{glyph.u_max, glyph.v_max}, line.color},
					{v2f32(pen_x + glyph.x_min_px, pen_y + glyph.y_max_px), v2u16{glyph.u_min, glyph.v_max}, line.color}
				};

				uint16_t indices[6] = { (uint16_t)(base+0), (uint16_t)(base+1), (uint16_t)(base+2),
                                        (uint16_t)(base+0), (uint16_t)(base+2), (uint16_t)(base+3) };

				vtx_mass.push(vertices, 4, indices, 6);
				pen_x += glyph.advance_px;
			}
		}
	}

	if (curr_font) flush(curr_font);
	sg_end_pass();
	return pass_stats;
}


void DebugPass::resize(const SurfaceInfo& surface_info)
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


void EnvironmentPass::execute(
	const geo::CanonicalShapes& canonical_shapes,
	BindingContext&             binding_ctx
)
{
	sg_image equirect_src_img = binding_ctx.environment.equirect_src_img;

	if (equirect_src_img.id == SG_INVALID_ID) {
		return;
	}

	const auto& box_geo_slice  = canonical_shapes.geo_slice[static_cast<size_t>(geo::CanonicalSubmesh::Box)];
	const auto& quad_geo_slice = canonical_shapes.geo_slice[static_cast<size_t>(geo::CanonicalSubmesh::Quad)];

	sg_pass_action pass_action_clear {};
	pass_action_clear.colors[0].load_action  = SG_LOADACTION_CLEAR;
	pass_action_clear.colors[0].store_action = SG_STOREACTION_STORE;
	pass_action_clear.colors[0].clear_value  = {0.0f, 0.0f, 0.0f, 1.0f};

	auto make_face_draw_view = [](sg_image target_img, uint32_t face_idx, uint32_t mip_level) -> sg_view
	{
		sg_view_desc view_desc {};
		view_desc.color_attachment.image     = target_img;
		view_desc.color_attachment.mip_level = mip_level;
		view_desc.color_attachment.slice     = face_idx;
		
		return sg_make_view(&view_desc);
	};

	const glm::mat4 mtx_P = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	
	const glm::mat4 mtxs_V[6] = {
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
		glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
	};

	sg_sampler smp_linear = binding_ctx.samplers.types[static_cast<size_t>(SamplerType::linear_clamp)];

	sg_bindings bindings_cube {};
	bindings_cube.vertex_buffers[0] = binding_ctx.gen_vtx.vtx_buf;
	bindings_cube.index_buffer      = binding_ctx.gen_vtx.idx_buf;

	{
		sg_view_desc source_view_desc {};
		source_view_desc.texture.image = equirect_src_img;
		sg_view source_view = sg_make_view(&source_view_desc);

		bindings_cube.views[VIEW_ibl_equirect_u_equirect]  = source_view;
		bindings_cube.samplers[SMP_ibl_equirect_u_sampler] = smp_linear;

		for (uint32_t face_index = 0; face_index < 6; ++face_index) {
			sg_view face_draw_view = make_face_draw_view(binding_ctx.environment.env_cube_img, face_index, 0);

			sg_attachments pass_attachments {};
			pass_attachments.colors[0] = face_draw_view;

			sg_pass render_pass {};
			render_pass.attachments = pass_attachments;
			render_pass.action      = pass_action_clear;

			sg_begin_pass(&render_pass);
			sg_apply_pipeline(binding_ctx.pipelines.ibl_equirect.pipeline);
			sg_apply_viewport(0, 0, 512, 512, true);
			
			sg_apply_bindings(&bindings_cube);

			ibl_equirect_u_vs_t vs_uniforms {};
			std::memcpy(vs_uniforms.mtx_VP, glm::value_ptr(mtx_P * mtxs_V[face_index]), sizeof(vs_uniforms.mtx_VP));
			sg_apply_uniforms(UB_ibl_equirect_u_vs, SG_RANGE(vs_uniforms));

			sg_draw_ex(
				static_cast<int>(box_geo_slice.idx_first),
				static_cast<int>(box_geo_slice.idx_count),
				1,
				0,
				0
			);

			sg_end_pass();
			sg_destroy_view(face_draw_view);
		}
		
		sg_destroy_view(source_view);
	}

	{
		bindings_cube.views[VIEW_ibl_irradiance_u_env_cube]  = binding_ctx.environment.env_view;
		bindings_cube.samplers[SMP_ibl_irradiance_u_sampler] = smp_linear;

		for (uint32_t face_index = 0; face_index < 6; ++face_index) {
			sg_view face_draw_view = make_face_draw_view(binding_ctx.environment.irr_cube_img, face_index, 0);

			sg_attachments pass_attachments {};
			pass_attachments.colors[0] = face_draw_view;

			sg_pass render_pass {};
			render_pass.attachments = pass_attachments;
			render_pass.action      = pass_action_clear;

			sg_begin_pass(&render_pass);
			sg_apply_pipeline(binding_ctx.pipelines.ibl_irradiance.pipeline);
			sg_apply_viewport(0, 0, 32, 32, true);
			
			sg_apply_bindings(&bindings_cube);

			ibl_irradiance_u_vs_t vs_uniforms {};
			std::memcpy(vs_uniforms.mtx_VP, glm::value_ptr(mtx_P * mtxs_V[face_index]), sizeof(vs_uniforms.mtx_VP));
			sg_apply_uniforms(UB_ibl_irradiance_u_vs, SG_RANGE(vs_uniforms));

			sg_draw_ex(
				static_cast<int>(box_geo_slice.idx_first),
				static_cast<int>(box_geo_slice.idx_count),
				1,
				0,
				0
			);

			sg_end_pass();
			sg_destroy_view(face_draw_view);
		}
	}

	{
		bindings_cube.views[VIEW_ibl_prefilter_u_env_cube]  = binding_ctx.environment.env_view;
		bindings_cube.samplers[SMP_ibl_prefilter_u_sampler] = smp_linear;

		const uint32_t max_mip_levels = 5;
		
		for (uint32_t mip_index = 0; mip_index < max_mip_levels; ++mip_index) {
			
			uint32_t mip_viewport_size = 128 >> mip_index;
			float current_roughness    = static_cast<float>(mip_index) / static_cast<float>(max_mip_levels - 1);
			
			for (uint32_t face_index = 0; face_index < 6; ++face_index) {
				sg_view face_draw_view = make_face_draw_view(binding_ctx.environment.pref_cube_img, face_index, mip_index);

				sg_attachments pass_attachments {};
				pass_attachments.colors[0] = face_draw_view;

				sg_pass render_pass {};
				render_pass.attachments = pass_attachments;
				render_pass.action      = pass_action_clear;

				sg_begin_pass(&render_pass);
				sg_apply_pipeline(binding_ctx.pipelines.ibl_prefilter.pipeline);
				sg_apply_viewport(0, 0, static_cast<int>(mip_viewport_size), static_cast<int>(mip_viewport_size), true);
				
				sg_apply_bindings(&bindings_cube);

				ibl_prefilter_u_vs_t vs_uniforms {};
				std::memcpy(vs_uniforms.mtx_VP, glm::value_ptr(mtx_P * mtxs_V[face_index]), sizeof(vs_uniforms.mtx_VP));
				sg_apply_uniforms(UB_ibl_prefilter_u_vs, SG_RANGE(vs_uniforms));

				ibl_prefilter_u_fs_t fs_uniforms {};
				fs_uniforms.roughness = current_roughness;
				sg_apply_uniforms(UB_ibl_prefilter_u_fs, SG_RANGE(fs_uniforms));

				sg_draw_ex(
					static_cast<int>(box_geo_slice.idx_first),
					static_cast<int>(box_geo_slice.idx_count),
					1,
					0,
					0
				);

				sg_end_pass();
				sg_destroy_view(face_draw_view);
			}
		}
	}

	{
		sg_view_desc lut_view_desc {};
		lut_view_desc.color_attachment.image = binding_ctx.environment.brdf_lut_img;
		sg_view lut_draw_view = sg_make_view(&lut_view_desc);

		sg_attachments pass_attachments {};
		pass_attachments.colors[0] = lut_draw_view;

		sg_pass render_pass {};
		render_pass.attachments = pass_attachments;
		render_pass.action      = pass_action_clear;

		sg_begin_pass(&render_pass);
		sg_apply_pipeline(binding_ctx.pipelines.ibl_brdf.pipeline);
		sg_apply_viewport(0, 0, 512, 512, true);

		sg_bindings bindings_quad {};
		bindings_quad.vertex_buffers[0] = binding_ctx.gen_vtx.vtx_buf;
		bindings_quad.index_buffer      = binding_ctx.gen_vtx.idx_buf;
		
		sg_apply_bindings(&bindings_quad);

		sg_draw_ex(
			static_cast<int>(quad_geo_slice.idx_first),
			static_cast<int>(quad_geo_slice.idx_count),
			1,
			0,
			0
		);

		sg_end_pass();
		sg_destroy_view(lut_draw_view);
	}

	sg_commit();
	
	sg_destroy_image(binding_ctx.environment.equirect_src_img);
	binding_ctx.environment.equirect_src_img.id = SG_INVALID_ID;
}


} // hpr::rdr

