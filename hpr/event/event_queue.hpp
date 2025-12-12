#pragma once

#include <memory>
#include <utility>

#include "panic.hpp"
#include "mtp_memory.hpp"


namespace hpr {


struct Event;
class LayerStack;


class EventQueue
{
	friend class LayerStack;

public:

	template<typename T, typename... Types>
	T* push(Types&&... args)
	{
		static_assert((std::is_base_of<Event, T>::value));

		auto event = std::make_unique<T>(std::forward<Types>(args)...);
		T* event_raw = event.get();

		m_pending.push_back(std::move(event));

		return event_raw;
	}

private:

	void freeze()
	{
		m_pending.swap(m_frozen);
	}

	mtp::vault<std::unique_ptr<Event>, mtp::default_set>& queue()
	{
		return m_frozen;
	}

	void clear()
	{
		m_frozen.clear();
	}

private:

	mtp::vault<std::unique_ptr<Event>, mtp::default_set> m_pending;
	mtp::vault<std::unique_ptr<Event>, mtp::default_set> m_frozen;
};

} // hpr
