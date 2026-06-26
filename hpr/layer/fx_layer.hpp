#pragma once

#include "hprint.hpp"

#include "layer.hpp"
#include "fx_data.hpp"
#include "editor_data.hpp"
#include "scene_context.hpp"

#include "render_queue.hpp"
#include "draw_queue_data.hpp"


namespace hpr {


namespace edt {

struct GridParams;

} // hpr::edt


class FxLayer : public Layer
{
public:

	FxLayer(
		edt::GridParams                   grid_params,
		rdr::RenderQueue<rdr::FxDrawCmd>& cmd_queue
	);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void set_grid_enabled(bool enabled);

private:

	static uint32_t rgb_u32(const glm::vec3& rgb);

private:

	rdr::GridPack m_grid_pack {};

	rdr::RenderQueue<rdr::FxDrawCmd>& m_cmd_queue;

	bool m_show_grid {true};
};

} // hpr

