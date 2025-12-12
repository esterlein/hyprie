#pragma once

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

#include "render_hub.hpp"
#include "render_data.hpp"
#include "render_queue.hpp"
#include "render_passes.hpp"
#include "frame_context.hpp"
#include "draw_queue_data.hpp"


#include "scene.glsl.h"


namespace hpr::rdr {


class Renderer
{

public:

	explicit Renderer(RenderHub& hub);

	SurfaceInfo surface_info() const
	{ return m_surface_info; }

	void frame();

	void handle_event(const sapp_event* event);
	void set_context(FrameContext context);
	void set_programs(RenderProgramSet programs);

	float world_size_per_pixel(const vec3& pos_world) const;

	void shutdown();

	const FrameContext& frame_context() const
	{ return m_frame_context; }

	RenderQueue<SceneDrawCommand>& scene_queue()
	{ return m_scene_queue; }

	RenderQueue<UiDrawCommand>& ui_queue()
	{ return m_ui_queue; }

	RenderQueue<DebugDrawCommand>& debug_queue()
	{ return m_debug_queue; }

	RenderQueue<OverlayDrawCommand>& overlay_queue()
	{ return m_overlay_queue; }

	RenderQueue<TileDrawCommand>& tile_queue()
	{ return m_tile_queue; }

	RenderQueue<FxDrawCommand>& fx_queue()
	{ return m_fx_queue; }

private:

	void surface_update();
	void clear_queues();

private:

	RenderHub& m_hub;

	RenderQueue<SceneDrawCommand>   m_scene_queue;
	RenderQueue<UiDrawCommand>      m_ui_queue;
	RenderQueue<DebugDrawCommand>   m_debug_queue;
	RenderQueue<OverlayDrawCommand> m_overlay_queue;
	RenderQueue<TileDrawCommand>    m_tile_queue;
	RenderQueue<FxDrawCommand>      m_fx_queue;

	FrameContext m_frame_context {};

	SurfaceInfo m_surface_info {};

	ScenePass      m_scene_pass;
	OutlinePass    m_outline_pass;
	CompositorPass m_compositor_pass;
	UiPass         m_ui_pass;
};

} // hpr::rdr

