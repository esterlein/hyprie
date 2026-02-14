#pragma once

#include "hprint.hpp"

#include <mutex>
#include <atomic>
#include <condition_variable>


namespace hpr::job {


class JobLatch
{
public:

	JobLatch() = default;

	void add(uint32_t additional_count)
	{
		m_pending_count.fetch_add(additional_count, std::memory_order_relaxed);
	}

	void done()
	{
		if (m_pending_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			m_condition.notify_all();
		}
	}

	void wait()
	{
		if (m_pending_count.load(std::memory_order_acquire) == 0) {
			return;
		}

		std::unique_lock<std::mutex> lock(m_mutex);
		m_condition.wait(lock,
			[this] {
				return m_pending_count.load(std::memory_order_acquire) == 0;
			}
		);
	}

private:

	std::atomic<uint32_t>   m_pending_count {0};
	std::mutex              m_mutex;
	std::condition_variable m_condition;
};


} // hpr::job
