#pragma once

#include <span>
#include <memory>
#include <concepts>

#include "sokol_app.h"

#include "event.hpp"
#include "renderer.hpp"
#include "command_stream.hpp"


namespace hpr {


struct Action;


class Layer
{
public:

	virtual ~Layer() = default;

	virtual void on_attach() = 0;
	virtual void on_detach() = 0;

	virtual bool on_event(Event& event)
	{
		(void) event;
		return false;
	}

	virtual bool on_actions(std::span<const Action> actions) = 0;
	virtual void on_update(float delta_time) = 0;
	virtual void on_submit(rdr::Renderer& renderer, uint32_t layer_index) = 0;

	template<std::derived_from<Layer> T, typename... Types>
	void transition_to(Types&&... args)
	{
		queue_transition(std::make_unique<T>(std::forward<Types>(args)...));
	}

	std::unique_ptr<Layer> take_transition()
	{
		return std::move(m_pending_transition);
	}

	virtual void process_commands(CmdStream::Reader reader)
	{
		(void) reader;
	}

private:

	void queue_transition(std::unique_ptr<Layer> layer)
	{
		m_pending_transition = std::move(layer);
	}

private:

	std::unique_ptr<Layer> m_pending_transition;
};

} // hpr

