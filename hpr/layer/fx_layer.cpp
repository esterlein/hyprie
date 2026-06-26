#include "fx_layer.hpp"

#include "draw_queue_data.hpp"
#include "math.hpp"
#include "fx_data.hpp"
#include "pixel_utils.hpp"
#include "render_data.hpp"
#include "render_queue.hpp"
#include "scene_context.hpp"


namespace hpr {


FxLayer::FxLayer(
	edt::GridParams                   grid_params,
	rdr::RenderQueue<rdr::FxDrawCmd>& cmd_queue
)
	: m_cmd_queue {cmd_queue}
{
	m_grid_pack = rdr::GridPack {
		.minor_rgba         = vec4(grid_params.minor_rgb, 1.0f),
		.major_rgba         = vec4(grid_params.major_rgb, 1.0f),
		.minor_vis_range_px = grid_params.minor_vis_range_px,
		.major_vis_range_px = grid_params.major_vis_range_px,
		
		.line_width_px      = grid_params.line_width_px,
		.cell_size          = grid_params.cell_size,
		.y_plane            = grid_params.y_plane,
		.major_step_cells   = static_cast<float>(grid_params.major_step_cells)
	};
}


void FxLayer::on_attach()
{}

void FxLayer::on_detach()
{}

bool FxLayer::on_event(Event& event)
{
	return false;
}

bool FxLayer::on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions)
{
	return false;
}

void FxLayer::on_update(scn::SceneContext& scn_ctx, float delta_time)
{}


void FxLayer::on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx)
{
	if (m_show_grid) {

		rdr::FxDrawCmd fx_cmd {
			.sort_key     = 100,
			.layer_idx    = layer_idx,
			.kind         = 0xFF,
			.payload_size = static_cast<uint8_t>(sizeof(rdr::GridPack))
		};

		std::memcpy(
			fx_cmd.payload.data(),
			&m_grid_pack,
			sizeof(rdr::GridPack)
		);

		m_cmd_queue.push(std::move(fx_cmd));
	}
}


void FxLayer::set_grid_enabled(bool enabled)
{
	m_show_grid = enabled;
}

} // hpr

