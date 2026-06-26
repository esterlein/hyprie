#pragma once

#include <cstdint>
#include <cassert>
#include <type_traits>

#include "mtp_memory.hpp"

#include "handle.hpp"


namespace hpr::res {


template<typename T>
class HandleStore
{
public:

	HandleStore(size_t capacity)
		: m_slots {capacity}
		, m_freed {capacity}
	{}

	~HandleStore()
	{
		for (uint32_t i = 0; i < m_slots.size(); ++i) {
			m_slots[i].destruct();
		}
	}

	HandleStore(const HandleStore&) = delete;
	HandleStore& operator=(const HandleStore&) = delete;

	HandleStore(HandleStore&& other) noexcept
		: m_slots(std::move(other.m_slots))
		, m_freed(std::move(other.m_freed))
		, m_active_count(other.m_active_count)
	{
		other.m_active_count = 0;
	}

	HandleStore& operator=(HandleStore&& other) noexcept
	{
		if (this != &other) {
			for (uint32_t i = 0; i < m_slots.size(); ++i) {
				m_slots[i].destruct();
			}

			m_slots = std::move(other.m_slots);
			m_freed = std::move(other.m_freed);
			m_active_count = other.m_active_count;

			other.m_active_count = 0;
		}
		return *this;
	}

	template<typename... Types>
	[[nodiscard]] Handle<T> create(Types&&... args)
	{
		uint32_t index;

		if (m_freed.size() > 0) {
			index = m_freed.back();
			m_freed.pop_back();
		}
		else {
			index = static_cast<uint32_t>(m_slots.size());
			m_slots.emplace_back();
		}

		Slot& slot = m_slots[index];
		slot.construct(std::forward<Types>(args)...);

		++m_active_count;

		return Handle<T> {index, slot.magic};
	}

	void destroy(Handle<T> handle)
	{
		if (handle.index >= m_slots.size()) {
			return;
		}

		Slot& slot = m_slots[handle.index];
		if (!slot.active) {
			return;
		}
		if (slot.magic != handle.magic) {
			return;
		}

		slot.destruct();
		m_freed.emplace_back(handle.index);
		--m_active_count;
	}

	[[nodiscard]] T* get(Handle<T> handle)
	{
		if (handle.index >= m_slots.size()) {
			return nullptr;
		}

		Slot& slot = m_slots[handle.index];
		if (!slot.active) {
			return nullptr;
		}
		if (slot.magic != handle.magic) {
			return nullptr;
		}

		return &slot.value();
	}

	[[nodiscard]] const T* get(Handle<T> handle) const
	{
		if (handle.index >= m_slots.size()) {
			return nullptr;
		}

		const Slot& slot = m_slots[handle.index];
		if (!slot.active) {
			return nullptr;
		}
		if (slot.magic != handle.magic) {
			return nullptr;
		}

		return &slot.value();
	}

	T& operator[](Handle<T> handle)
	{ return *get(handle); }

	const T& operator[](Handle<T> handle) const
	{ return *get(handle); }

	uint32_t size() const
	{ return m_active_count; }

	uint32_t capacity() const
	{ return static_cast<uint32_t>(m_slots.size()); }

private:

	struct Slot
	{
		alignas(T) unsigned char storage[sizeof(T)];

		uint32_t magic {0};

		bool active {false};

		template<typename... Args>
		void construct(Args&&... args)
		{
			assert(!active);

			new (storage) T(std::forward<Args>(args)...);
			active = true;
			++magic;
		}

		void destruct()
		{
			if (!active) {
				return;
			}

			std::launder(reinterpret_cast<T*>(storage))->~T();
			active = false;
			++magic;
		}

		T& value()
		{
			return *std::launder(reinterpret_cast<T*>(storage));
		}

		const T& value() const
		{
			return *std::launder(reinterpret_cast<const T*>(storage));
		}
	};

	mtp::slag<Slot,     mtp::default_set> m_slots;
	mtp::slag<uint32_t, mtp::default_set> m_freed;

	uint32_t m_active_count {0};
};

} // hpr::res

