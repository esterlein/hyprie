#pragma once

#include <span>

#include "engine.hpp"
#include "layer.hpp"
#include "event.hpp"
#include "event_emitter.hpp"
#include "renderer.hpp"
#include "ui_backend.hpp"
#include "ui_style.hpp"
#include "ui_context.hpp"
#include "ui_resolver.hpp"
#include "editor_resolver.hpp"

#include "entity.hpp"
#include "ui_inspector.hpp"

#include "input_state.hpp"
#include "command_emitter.hpp"


namespace hpr {


class EditLayer : public Layer, public EventEmitter, public CommandEmitter
{
public:

	EditLayer(
		rdr::Renderer&             renderer,
		io::InputState&            input_state,
		ui::UiBackend&             ui_backend,
		const edt::EditorResolver& editor_resolver
	)
		: m_renderer        {renderer}
		, m_input_state     {input_state}
		, m_ui_backend      {ui_backend}
		, m_editor_resolver {editor_resolver}
	{}

	void on_attach() override;
	void on_detach() override;

	bool on_event(Event& event) override;
	void on_result(Event& event) override;

	bool on_actions(std::span<const Action> actions) override;
	void on_update(float delta_time) override;
	void on_submit(rdr::Renderer& renderer, uint32_t layer_index) override;

	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void set_command_stream(hpr::CmdStream& stream) override
	{ m_cmd_stream = &stream; }

private:

	rdr::Renderer&    m_renderer;
	io::InputState&   m_input_state;

	ui::UiBackend& m_ui_backend;
	ui::UiContext  m_ui_context;
	ui::UiStyle    m_ui_style;

	const edt::EditorResolver& m_editor_resolver;
	edt::InspectorState        m_inspector_state {};

	EventQueue* m_event_queue {nullptr};
	CmdStream*  m_cmd_stream  {nullptr};
};

} // hpr

