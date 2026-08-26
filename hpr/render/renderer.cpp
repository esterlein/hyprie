#include "hprint.hpp"

#include "renderer.hpp"

#include "math.hpp"
#include "stats.hpp"
#include "surface.hpp"
#include "ui_alloc.hpp"
#include "render_data.hpp"
#include "storage_mass.hpp"
#include "event_dispatcher.hpp"

#include "scene.glsl.h"
#include "overlay.glsl.h"
#include "grid.glsl.h"
#include "bitmap.glsl.h"
#include "outline_mask.glsl.h"
#include "outline_dilate.glsl.h"
#include "outline_blend.glsl.h"

#include "sokol_log.h"


namespace hpr::rdr {


Renderer::Renderer(
	RenderHub&           hub,
	const SurfaceInfo&   surface_info,
	log::StatsHarvester& harvester
)
	: m_hub             {hub}
	, m_surface_info    {surface_info}
	, m_scene_queue     {1024}
	, m_anim_queue      {1024}
	, m_replay_queue    {1024}
	, m_ui_queue        {1024}
	, m_debug_queue     {256}
	, m_overlay_queue   {1024}
	, m_cue_queue       {1024}
	, m_fx_queue        {1024}
	, m_scene_pass      {hub}
	, m_outline_pass    {hub}
	, m_compositor_pass {hub}
	, m_ui_pass         {hub}
	, m_debug_pass      {hub}
	, m_harvester       {harvester}
{
	m_ui_pass.init();

	m_ui_pass.resize(m_surface_info);
	m_debug_pass.resize(m_surface_info);
}


void Renderer::frame()
{
	auto& stats = m_harvester.frame_curr();

	auto begin_time = std::chrono::steady_clock::now();

	stats.scene = m_scene_pass.execute(
		m_scene_queue,
		m_anim_queue,
		m_replay_queue,
		m_scene_ctx,
		m_binding_ctx,
		m_staging_ctx
	);
	
	stats.total = stats.scene;

	stats.total += m_compositor_pass.execute(
		m_fx_queue,
		m_cue_queue,
		m_overlay_queue,
		m_scene_ctx,
		m_binding_ctx,
		m_staging_ctx,
		m_surface_info
	);

	if (!m_replay_queue.empty()) {
		stats.total += m_outline_pass.execute(
			m_replay_queue,
			m_scene_ctx,
			m_binding_ctx,
			m_surface_info
		);
	}

	stats.total += m_ui_pass.execute(
		m_ui_queue,
		m_surface_info,
		m_binding_ctx
	);

	stats.total += m_debug_pass.execute(
		m_debug_queue,
		m_binding_ctx,
		m_staging_ctx,
		m_surface_info
	);

	sg_commit();

	const auto end_time = std::chrono::steady_clock::now();
	
	stats.rdr_cpu_time =
		std::chrono::duration<double, std::milli>(end_time - begin_time).count();

	clear_frame();
}


bool Renderer::on_event(Event& event)
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


void Renderer::set_scene_context(const scn::SceneContext& scn_ctx)
{
	m_scene_ctx = scn_ctx;
}


void Renderer::set_render_context(
	BindingContext              binding_ctx,
	StagingContext              staging_ctx,
	const geo::CanonicalShapes& shapes
)
{
	m_binding_ctx = std::move(binding_ctx);
	m_staging_ctx = std::move(staging_ctx);

	m_scene_pass.set_canonical_shapes(shapes);
	m_environment_pass.execute(shapes, m_binding_ctx);
}


void Renderer::clear_frame()
{
	m_scene_queue.clear();
	m_anim_queue.clear();
	m_fx_queue.clear();
	m_overlay_queue.clear();
	m_ui_queue.clear();
	m_debug_queue.clear();
	m_cue_queue.clear();
}


void Renderer::shutdown()
{
	m_ui_pass.release();
}

} // hpr::rdr

