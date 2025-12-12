#pragma once

#include <cstdint>

#include "editor_data.hpp"
#include "layer.hpp"
#include "renderer.hpp"
#include "scene_data.hpp"


namespace hpr {

namespace edt {

struct GridParams;

} // edt


class FxLayer : public Layer
{
public:

	FxLayer(
		rdr::Renderer&  renderer,
		edt::GridParams grid_params
	);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;
	bool on_actions(std::span<const Action> actions) override;
	void on_update(float delta_time) override;
	void on_submit(rdr::Renderer& renderer, uint32_t layer_index) override;

	void set_grid_enabled(bool enabled);

private:

	static uint32_t rgb_u32(const glm::vec3& rgb);

private:

	rdr::Renderer&  m_renderer;
	edt::GridParams m_grid_params {};

	bool m_show_grid {true};
};

} // hpr

