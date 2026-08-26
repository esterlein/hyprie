#pragma once

#include "hprint.hpp"

#include "stats.hpp"
#include "handle.hpp"
#include "surface.hpp"
#include "nuklear_cfg.hpp"

#include "render_hub.hpp"
#include "render_data.hpp"
#include "render_queue.hpp"
#include "render_context.hpp"
#include "canonical_data.hpp"
#include "draw_queue_data.hpp"

#include "scene_context.hpp"

#include "sokol_gfx.h"
#include "sokol_glue.h"


namespace hpr::rdr {


class ScenePass
{
public:

	explicit ScenePass(RenderHub& hub)
		: m_hub {hub}
	{}

	log::PassStats execute(
		RenderQueue<SceneDrawCmd>&  scene_queue,
		RenderQueue<AnimDrawCmd>&   anim_queue,
		RenderQueue<ReplayDrawCmd>& replay_queue,
		const scn::SceneContext&    scene_ctx,
		const BindingContext&       binding_ctx,
		const StagingContext&       staging_ctx
	);

	void set_canonical_shapes(const geo::CanonicalShapes& shapes)
	{
		m_canonical_shapes = shapes;
	};

private:

	RenderHub& m_hub;
	geo::CanonicalShapes m_canonical_shapes {};
};


class OutlinePass
{
public:

	explicit OutlinePass(RenderHub& hub)
		: m_hub {hub}
	{}

	~OutlinePass() = default;

	void set_outline_params(uint32_t color_rgb888, uint32_t alpha_visible, int radius_px);

	log::PassStats execute(
		RenderQueue<ReplayDrawCmd>& replay_queue,
		const scn::SceneContext&    scene_ctx,
		const BindingContext&       binding_ctx,
		const SurfaceInfo&          surface_info
	);

private:

	RenderHub& m_hub;

	uint32_t m_rgb888 {0xFFFFFFFF};
	uint32_t m_alpha  {0xFFFFFFFF};

	int m_radius_px {2};
};


class CompositorPass
{
public:

	explicit CompositorPass(RenderHub& hub)
		: m_hub {hub}
	{}

	~CompositorPass() = default;

	log::PassStats execute(
		RenderQueue<FxDrawCmd>&      fx_queue,
		RenderQueue<CueDrawCmd>&     cue_queue,
		RenderQueue<OverlayDrawCmd>& overlay_queue,
		const scn::SceneContext&     scene_ctx,
		const BindingContext&        binding_ctx,
		StagingContext&              staging_ctx,
		const SurfaceInfo&           surface_info
	);

private:

	RenderHub& m_hub;
};



class UiPass
{
public:

	explicit UiPass(RenderHub& hub)
		: m_hub {hub}
	{}

	~UiPass()
	{
		release();
	}

	void init();

	void resize(const SurfaceInfo& surface_info);

	log::PassStats execute(
		RenderQueue<UiDrawCmd>& queue,
		const SurfaceInfo&      surface_info,
		const BindingContext&   binding_ctx
	);

	void release();

private:

	RenderHub& m_hub;

	sg_buffer m_ui_vtx_buf {};
	sg_buffer m_ui_idx_buf {};

	mat4 m_mtx_P_ortho {1.0f};

	nk_buffer m_cmd_buf {};
	nk_buffer m_vtx_buf {};
	nk_buffer m_idx_buf {};

	nk_size m_cmd_capacity {1 *  256U * 1024U};
	nk_size m_vtx_capacity {2 * 1024U * 1024U};
	nk_size m_idx_capacity {1 * 1024U * 1024U};

	nk_convert_config m_ui_cfg {};
	nk_draw_vertex_layout_element m_ui_layout[4] {};
};



class DebugPass
{
public:

	explicit DebugPass(RenderHub& hub)
		: m_hub {hub}
	{}

	~DebugPass() = default;

	void resize(const SurfaceInfo& surface_info);

	log::PassStats execute(
		RenderQueue<DebugDrawCmd>& queue,
		const BindingContext&      binding_ctx,
		StagingContext&            staging_ctx,
		const SurfaceInfo&         surface_info
	);

private:

	RenderHub& m_hub;

	mat4 m_mtx_P_ortho {1.0f};
};


class EnvironmentPass
{
public:
	EnvironmentPass() = default;

public:
	void execute(
		const geo::CanonicalShapes& canonical_shapes,
		BindingContext&             binding_ctx
	);
};


} // hpr::rdr

