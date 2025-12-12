#include "fx_layer.hpp"

#include "color.hpp"
#include "fx_data.hpp"
#include "render_data.hpp"


namespace hpr {


FxLayer::FxLayer(
	rdr::Renderer&  renderer,
	edt::GridParams grid_params
)
	: m_renderer    {renderer}
	, m_grid_params {std::move(grid_params)}
{}

void FxLayer::on_attach()
{}

void FxLayer::on_detach()
{}

bool FxLayer::on_event(Event& event)
{
	return false;
}

bool FxLayer::on_actions(std::span<const Action> actions)
{
	return false;
}

void FxLayer::on_update(float delta_time)
{}

void FxLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	if (m_show_grid) {

		rdr::GridPack grig_pack {
			.base_spacing  = m_grid_params.base_spacing,
			.target_px     = m_grid_params.target_px,
			.line_width_px = m_grid_params.line_width_px,
			.major_step    = m_grid_params.major_step,
			.minor_rgb888  = rgb_to_u32(m_grid_params.color_minor_rgb),
			.major_rgb888  = rgb_to_u32(m_grid_params.color_major_rgb),
			.grid_y        = 0.0f
		};

		rdr::FxDrawCommand fx_cmd {
			.sort_key    = 100,
			.layer_index = layer_index,
			.kind        = static_cast<uint8_t>(rdr::ProgramFlag::Grid),
			.params_size = static_cast<uint8_t>(sizeof(rdr::GridPack))
		};

		std::memcpy(fx_cmd.params.data(), &grig_pack, sizeof(rdr::GridPack));

		renderer.fx_queue().push(fx_cmd);
	}
}

void FxLayer::set_grid_enabled(bool enabled)
{
	m_show_grid = enabled;
}

} // hpr

