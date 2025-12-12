#pragma once

#include <algorithm>

#include "mtp_memory.hpp"


namespace hpr::rdr {


template <typename T>
class RenderQueue
{
public:

	explicit RenderQueue(size_t capacity)
		: m_commands {capacity}
	{}

	void clear()
	{
		m_commands.clear();
	}

	void push(const T& command)
	{
		m_commands.emplace_back(command);
	}

	T& back()
	{
		return m_commands.back();
	}

	void sort()
	{
		std::sort(m_commands.begin(), m_commands.end(), [](const T& left, const T& right)
		{
			return left.sort_key < right.sort_key;
		});
	}

	bool empty() const
	{
		return m_commands.empty();
	}

	uint32_t size() const
	{
		return m_commands.size();
	}

	const mtp::slag<T, mtp::default_set>& commands() const
	{
		return m_commands;
	}

private:

	mtp::slag<T, mtp::default_set> m_commands;
};

} // hpr::rdr

