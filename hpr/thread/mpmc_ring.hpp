#pragma once

#include "hprint.hpp"

#include <new>
#include <array>
#include <atomic>
#include <utility>


namespace hpr::job {


template <typename T, uint32_t Capacity>
requires (Capacity != 0 && ((Capacity & (Capacity - 1)) == 0))
class MpmcRing
{
public:

	MpmcRing()
	{
		for (uint32_t i = 0; i < Capacity; ++i) {
			m_slots[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	~MpmcRing() = default;

	MpmcRing(const MpmcRing&) = delete;
	MpmcRing& operator=(const MpmcRing&) = delete;

public:

	bool push(const T& value)
	{
		uint32_t tail = m_tail.load(std::memory_order_relaxed);

		for (;;) {

			Slot& slot = m_slots[tail & (Capacity - 1)];

			const uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
			const int32_t diff = static_cast<int32_t>(sequence) - static_cast<int32_t>(tail);

			if (diff == 0) {

				if (m_tail.compare_exchange_weak(
					tail,
					tail + 1,
					std::memory_order_relaxed,
					std::memory_order_relaxed
				)) {
					slot.value = value;
					slot.sequence.store(tail + 1, std::memory_order_release);
					return true;
				}

				continue;
			}

			if (diff < 0) {
				return false;
			}

			tail = m_tail.load(std::memory_order_relaxed);
		}
	}

	bool push(T&& value)
	{
		uint32_t tail = m_tail.load(std::memory_order_relaxed);

		for (;;) {

			Slot& slot = m_slots[tail & (Capacity - 1)];

			const uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
			const int32_t diff = static_cast<int32_t>(sequence) - static_cast<int32_t>(tail);

			if (diff == 0) {

				if (m_tail.compare_exchange_weak(
					tail,
					tail + 1,
					std::memory_order_relaxed,
					std::memory_order_relaxed
				)) {
					slot.value = std::move(value);
					slot.sequence.store(tail + 1, std::memory_order_release);
					return true;
				}

				continue;
			}

			if (diff < 0) {
				return false;
			}

			tail = m_tail.load(std::memory_order_relaxed);
		}
	}

	bool pop(T& value)
	{
		for (;;) {

			uint32_t head = m_head.load(std::memory_order_relaxed);
			Slot& slot = m_slots[head & (Capacity - 1)];

			const uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
			const int32_t diff = static_cast<int32_t>(sequence) - static_cast<int32_t>(head + 1);

			if (diff < 0) {
				return false;
			}

			if (diff == 0) {

				if (m_head.compare_exchange_weak(
					head,
					head + 1,
					std::memory_order_acq_rel,
					std::memory_order_relaxed
				)) {
					value = slot.value;
					slot.sequence.store(head + Capacity, std::memory_order_release);
					return true;
				}
			}
		}
	}

	uint32_t approx_size() const
	{
		const uint32_t head = m_head.load(std::memory_order_relaxed);
		const uint32_t tail = m_tail.load(std::memory_order_relaxed);
		return tail - head;
	}

	void reset()
	{
		m_head.store(0, std::memory_order_relaxed);
		m_tail.store(0, std::memory_order_relaxed);

		for (uint32_t i = 0; i < Capacity; ++i) {
			m_slots[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

private:

	struct Slot
	{
		std::atomic<uint32_t> sequence;
		T value;
	};

	alignas(std::hardware_destructive_interference_size) std::atomic<uint32_t> m_head {0};
	alignas(std::hardware_destructive_interference_size) std::atomic<uint32_t> m_tail {0};

	std::array<Slot, Capacity> m_slots {};
};


} // hpr::job

