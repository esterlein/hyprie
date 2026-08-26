#pragma once

#include "hprint.hpp"

#include "event.hpp"
#include "stats.hpp"
#include "surface.hpp"

#include "render_hub.hpp"
#include "render_data.hpp"
#include "render_queue.hpp"
#include "render_passes.hpp"
#include "scene_context.hpp"
#include "render_context.hpp"
#include "draw_queue_data.hpp"


#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

#include <chrono>


namespace hpr::rdr {


class Renderer
{

public:

	explicit Renderer(
		RenderHub&           hub,
		const SurfaceInfo&   surface_info,
		log::StatsHarvester& harvester
	);

	void frame();

	bool on_event(Event& event);
	void set_scene_context(const scn::SceneContext& scene_ctx);

	void set_render_context(
		BindingContext              binding_ctx,
		StagingContext              staging_ctx,
		const geo::CanonicalShapes& shapes
	);

	void shutdown();

	const scn::SceneContext& scene_context() const
	{ return m_scene_ctx; }

	RenderQueue<SceneDrawCmd>& scene_queue()
	{ return m_scene_queue; }

	RenderQueue<AnimDrawCmd>& anim_queue()
	{ return m_anim_queue; }

	RenderQueue<UiDrawCmd>& ui_queue()
	{ return m_ui_queue; }

	RenderQueue<DebugDrawCmd>& debug_queue()
	{ return m_debug_queue; }

	RenderQueue<OverlayDrawCmd>& overlay_queue()
	{ return m_overlay_queue; }

	RenderQueue<CueDrawCmd>& cue_queue()
	{ return m_cue_queue; }

	RenderQueue<FxDrawCmd>& fx_queue()
	{ return m_fx_queue; }

private:

	void clear_frame();

private:

	RenderHub&  m_hub;
	SurfaceInfo m_surface_info {};

	RenderQueue<SceneDrawCmd>   m_scene_queue;
	RenderQueue<AnimDrawCmd>    m_anim_queue;
	RenderQueue<ReplayDrawCmd>  m_replay_queue;
	RenderQueue<UiDrawCmd>      m_ui_queue;
	RenderQueue<DebugDrawCmd>   m_debug_queue;
	RenderQueue<OverlayDrawCmd> m_overlay_queue;
	RenderQueue<CueDrawCmd>     m_cue_queue;
	RenderQueue<FxDrawCmd>      m_fx_queue;

	BindingContext    m_binding_ctx {};
	StagingContext    m_staging_ctx {};
	scn::SceneContext m_scene_ctx   {};

	ScenePass       m_scene_pass;
	OutlinePass     m_outline_pass;
	CompositorPass  m_compositor_pass;
	UiPass          m_ui_pass;
	DebugPass       m_debug_pass;
	EnvironmentPass m_environment_pass;

	log::StatsHarvester& m_harvester;
};

} // hpr::rdr

