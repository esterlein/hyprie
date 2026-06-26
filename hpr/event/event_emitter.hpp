#pragma once

#include "event_queue.hpp"


namespace hpr {


class EventEmitter
{
public:

	virtual ~EventEmitter() = default;

	virtual void set_event_queue(EventQueue& queue) = 0;
	virtual void on_result(Event& event) = 0;
};

} // hpr
