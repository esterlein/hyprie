#include "render_data.hpp"

#include "sokol_log.h"

#include "math.hpp"
#include "ui_alloc.hpp"
#include "renderer.hpp"

#include "scene.glsl.h"
#include "overlay.glsl.h"
#include "ui.glsl.h"
#include "grid.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_dilate.glsl.h"
#include "outline_blend.glsl.h"


namespace hpr::rdr {


// TODO: initialize passes with programs in ctor


Renderer::Renderer(RenderHub& hub)
	: m_hub             {hub}
	, m_scene_queue     {1024}
	, m_ui_queue        {1024}
	, m_debug_queue     {1024}
	, m_overlay_queue   {1024}
	, m_tile_queue      {1024}
	, m_fx_queue        {1024}
	, m_scene_pass      {hub}
	, m_outline_pass    {hub}
	, m_compositor_pass {hub}
	, m_ui_pass         {hub}
{
	surface_update();

	m_ui_pass.init();
	m_outline_pass.init();
	m_compositor_pass.init();

	m_scene_pass.resize(m_surface_info);
	m_outline_pass.resize(m_surface_info);
	m_compositor_pass.resize(m_surface_info);
	m_ui_pass.resize(m_surface_info);
}


void Renderer::frame()
{
	m_scene_pass.execute(m_scene_queue, m_frame_context);

	m_compositor_pass.execute(m_fx_queue, m_tile_queue, m_overlay_queue, m_frame_context, m_surface_info);

	const auto& replay_tokens = m_scene_pass.get_replay_tokens();
	if (!replay_tokens.empty()) {
		m_outline_pass.execute(replay_tokens, m_frame_context);
	}

	m_ui_pass.execute(m_ui_queue, m_surface_info);

	sg_commit();

	clear_queues();


static uint64_t frame_count = 0;
static auto last_time = std::chrono::steady_clock::now();

++frame_count;

auto now = std::chrono::steady_clock::now();
auto elapsed = std::chrono::duration<double>(now - last_time).count();

if (elapsed >= 1.0) {
	double fps = frame_count / elapsed;
	std::printf("fps: %.1f (%.2f ms)\n", fps, 1000.0 / fps);
	std::fflush(stdout);

	frame_count = 0;
	last_time = now;
}

}


void Renderer::handle_event(const sapp_event* event)
{
	if (event && event->type == SAPP_EVENTTYPE_RESIZED) {
		surface_update();

		m_scene_pass.resize(m_surface_info);
		m_outline_pass.resize(m_surface_info);
		m_compositor_pass.resize(m_surface_info);
		m_ui_pass.resize(m_surface_info);
	}
}


void Renderer::set_context(FrameContext context)
{
	m_frame_context = std::move(context);
}


// TODO: initialize passes with the aggregate struct ref in ctor

void Renderer::set_programs(RenderProgramSet programs)
{
	if (programs.prog_grid.is_valid()) {
		m_compositor_pass.set_programs(programs.prog_grid, programs.prog_tile, programs.prog_overlay);
	}
	if (programs.prog_mask.is_valid() && programs.prog_outline_dilate.is_valid() && programs.prog_outline_blend.is_valid()) {
		m_outline_pass.set_programs(programs.prog_mask, programs.prog_outline_dilate, programs.prog_outline_blend);
	}
	if (programs.prog_ui.is_valid()) {
		m_ui_pass.set_program(programs.prog_ui);
	}
}


void Renderer::surface_update()
{
	m_surface_info.width  = sapp_width();
	m_surface_info.height = sapp_height();

	if (m_surface_info.width  <= 0)
		m_surface_info.width   = 1;
	if (m_surface_info.height <= 0)
		m_surface_info.height  = 1;

	m_surface_info.aspect =
		static_cast<float>(m_surface_info.width) / static_cast<float>(m_surface_info.height);

	m_surface_info.dpi = sapp_dpi_scale();

	m_surface_info.sample_count = sglue_swapchain().sample_count;
	m_surface_info.color_format = sglue_swapchain().color_format;
	m_surface_info.depth_format = sglue_swapchain().depth_format;
}


float Renderer::world_size_per_pixel(const vec3& pos_world) const
{
	const vec4  pos_view   = m_frame_context.scene_view.mtx_V * vec4(pos_world, 1.0f);
	const float depth_pos  = glm::max(0.0f, -pos_view.z);
	const float height_px  = static_cast<float>(m_surface_info.height > 0 ? m_surface_info.height : 1);

	const bool is_perspective =
		glm::epsilonEqual(m_frame_context.scene_view.mtx_P[2][3], -1.0f, math::projection_epsilon) ||
		glm::epsilonEqual(m_frame_context.scene_view.mtx_P[3][3],  0.0f, math::projection_epsilon);

	const float projection_y_scale = m_frame_context.scene_view.mtx_P[1][1];

	if (is_perspective) {
		const float tan_half_y_fov = 1.0f / projection_y_scale;
		return (2.0f * depth_pos * tan_half_y_fov) / height_px;
	}
	else {
		const float world_height = 2.0f / projection_y_scale;
		return world_height / height_px;
	}
}


void Renderer::clear_queues()
{
	m_scene_queue.clear();
	m_fx_queue.clear();
	m_overlay_queue.clear();
	m_ui_queue.clear();
	m_debug_queue.clear();
	m_tile_queue.clear();
}


void Renderer::shutdown()
{
	m_ui_pass.release();
	//m_overlay_pass.release();
	m_compositor_pass.release();
	m_outline_pass.release();
	//m_scene_pass.release();
}

} // hpr::rdr

