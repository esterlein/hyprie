#pragma once

#include "layer.hpp"
#include "event.hpp"
#include "event_emitter.hpp"
#include "command_emitter.hpp"

#include "render_queue.hpp"
#include "draw_queue_data.hpp"

#include "surface.hpp"
#include "input_state.hpp"
#include "scene_context.hpp"
#include "editor_resolver.hpp"

#include "ui_style.hpp"
#include "ui_context.hpp"
#include "ui_backend.hpp"
#include "ui_resolver.hpp"
#include "ui_inspector.hpp"

#include <span>


namespace hpr::lyr {


class EditLayer : public Layer, public EventEmitter, public CommandEmitter
{
public:

	EditLayer(
		io::InputState&                   input_state,
		ui::UiBackend&                    ui_backend,
		rdr::SurfaceInfo                  surface_info,
		const edt::EditorResolver&        editor_resolver,
		rdr::RenderQueue<rdr::UiDrawCmd>& cmd_queue
	)
		: m_input_state     {input_state}
		, m_ui_backend      {ui_backend}
		, m_surface_info    {surface_info}
		, m_editor_resolver {editor_resolver}
		, m_cmd_queue       {cmd_queue}
	{}

	void on_attach() override;
	void on_detach() override;

	bool on_event(Event& event) override;
	void on_result(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const io::Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void set_command_stream(hpr::CmdStream& stream) override
	{ m_cmd_stream = &stream; }

private:

	io::InputState& m_input_state;

	ui::UiBackend& m_ui_backend;
	ui::UiContext  m_ui_context;
	ui::UiStyle    m_ui_style;

	rdr::SurfaceInfo           m_surface_info;
	const edt::EditorResolver& m_editor_resolver;
	edt::InspectorState        m_inspector_state {};

	rdr::RenderQueue<rdr::UiDrawCmd>& m_cmd_queue;

	EventQueue* m_event_queue {nullptr};
	CmdStream*  m_cmd_stream  {nullptr};
};

} // hpr::lyr

