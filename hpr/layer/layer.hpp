#pragma once

#include "event.hpp"
#include "action.hpp"
#include "scene_context.hpp"
#include "command_stream.hpp"

#include "sokol_app.h"

#include <span>
#include <memory>
#include <concepts>


namespace hpr::io {

struct Action;

} // hpr::io


namespace hpr::lyr {


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

	virtual bool on_actions(const scn::SceneContext& scn_ctx, std::span<const io::Action> actions) = 0;
	virtual void on_update(scn::SceneContext& scn_ctx, float delta_time) = 0;
	virtual void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) = 0;

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

} // hpr::lyr

