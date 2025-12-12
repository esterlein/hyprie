#pragma once

#include <cstdint>

#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "nuklear_cfg.hpp"

#include "handle.hpp"
#include "render_hub.hpp"
#include "render_data.hpp"
#include "render_queue.hpp"
#include "frame_context.hpp"
#include "draw_queue_data.hpp"

#include "scene.glsl.h"


namespace hpr::rdr {


struct SceneReplayToken
{
	Handle<Mesh>    mesh;
	uint32_t        submesh_idx;
	mat4            mtx_model;
	Handle<Program> program;
};



class ScenePass
{
public:

	explicit ScenePass(RenderHub& hub)
		: m_hub           {hub}
		, m_replay_tokens {1024}
	{}

	void resize(const SurfaceInfo& surface_info);

	void execute(RenderQueue<SceneDrawCommand>& queue, const FrameContext& context);

	const mtp::vault<SceneReplayToken, mtp::default_set>& get_replay_tokens() const
	{ return m_replay_tokens; }

	void clear_replays()
	{ m_replay_tokens.clear(); }

private:

	void set_view_light(const rdr::DrawViewLightSet& scene_light);

private:

	RenderHub& m_hub;

	scene_fs_light_params_t m_light_ubo {};

	mtp::vault<SceneReplayToken, mtp::default_set> m_replay_tokens;
};



class OutlinePass
{
public:

	explicit OutlinePass(RenderHub& hub)
		: m_hub {hub}
	{}

	~OutlinePass()
	{
		release();
	}

	void init();

	void resize(const SurfaceInfo& surface_info);

	void set_programs(Handle<Program> prog_mask, Handle<Program> prog_dilate, Handle<Program> prog_blend);
	void set_outline_params(uint32_t color_rgb888, uint32_t alpha_visible, int radius_px);

	void execute(const mtp::vault<SceneReplayToken, mtp::default_set>& selected_items, const FrameContext& context);

	void release();

private:

	void recreate_images(int width, int height);

private:

	RenderHub& m_hub;

	Handle<Program> m_prog_mask   {};
	Handle<Program> m_prog_dilate {};
	Handle<Program> m_prog_blend  {};

	sg_image m_image_mask   {};
	sg_image m_image_dilate {};

	sg_attachments m_attachments_mask   {};
	sg_attachments m_attachments_dilate {};

	sg_sampler m_sampler_nearest {};
	sg_sampler m_sampler_linear  {};

	int m_width  {1};
	int m_height {1};

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

	~CompositorPass()
	{
		release();
	}

	void init();

	void resize(const SurfaceInfo& surface_info);

	// TODO: initialize with the aggregate struct ref in ctor
	void set_programs(Handle<Program> prog_grid, Handle<Program> prog_tile, Handle<Program> prog_overlay);

	void execute(
		RenderQueue<FxDrawCommand>&      fx_queue,
		RenderQueue<TileDrawCommand>&    tile_queue,
		RenderQueue<OverlayDrawCommand>& overlay_queue,
		const FrameContext&              context,
		const SurfaceInfo&               surface_info
	);

	void release();

private:

	RenderHub& m_hub;

	SurfaceInfo m_surface_info {};

	Handle<Program> m_prog_grid    {};
	Handle<Program> m_prog_tile    {};
	Handle<Program> m_prog_overlay {};
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
	void set_program(Handle<Program> prog_ui);

	void execute(RenderQueue<UiDrawCommand>& queue, const SurfaceInfo& surface_info);

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

	Handle<Program> m_prog_ui {};
};

} // hpr::rdr

