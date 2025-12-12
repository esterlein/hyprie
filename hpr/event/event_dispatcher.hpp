#pragma once

#include "event.hpp"


namespace hpr {


class EventDispatcher
{
public:

	explicit EventDispatcher(Event& event)
		: m_event {event}
	{}

	template<typename T, typename Func>
	bool dispatch(const Func& func)
	{
		if (m_event.get_event_type() == T::get_static_type()) {

			T& event = static_cast<T&>(m_event);

			(void) func(event); 

			return true;
		}
		return false;
	}

private:

	Event& m_event;
};


#define HPR_BIND_EVENT_FUNC(func) [this](auto&&... args) -> decltype(auto) { return this->func(std::forward<decltype(args)>(args)...); }

} // hpr
