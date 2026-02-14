#pragma once

#include <array>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "panic.hpp"
#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "job_latch.hpp"
#include "mpmc_ring.hpp"


namespace hpr::job {


using JobFn = void (*)(void* job_fn_input);


struct JobEntry
{
	JobFn     function;
	void*     fn_input;
	JobLatch* latch;
};


namespace cfg {


inline constexpr uint32_t max_workers      = 32U;
inline constexpr uint32_t deque_capacity   = 1024U;
inline constexpr uint32_t mtp_deque_stride =
	((sizeof(JobEntry) * cfg::deque_capacity + 2U + 7U) / 8U) * 8U;

inline constexpr uint32_t injection_capacity = max_workers * deque_capacity;

} // hpr::job::cfg


class Scheduler
{
public:

	Scheduler() = default;

	Scheduler(const Scheduler&) = delete;
	Scheduler& operator=(const Scheduler&) = delete;

private:

	using mtp_job_set = mtp::metaset <

		mtp::def <
			mtp::capf::flat,
			cfg::max_workers,
			8,
			cfg::mtp_deque_stride,
			cfg::mtp_deque_stride
		>
	>;

	struct Worker
	{
		std::mutex              mutex;
		std::condition_variable condition;

		std::atomic<uint32_t>   has_work {0};
	};

public:

	void init(uint32_t worker_count)
	{
		HPR_ASSERT_MSG(worker_count <= cfg::max_workers,
			"worker count > max workers");

		shutdown();

		m_worker_count = worker_count;

		for (uint32_t worker_index = 0; worker_index < m_worker_count; ++worker_index) {

			m_job_deques.construct(worker_index, m_metapool, cfg::deque_capacity);

			m_worker_threads[worker_index] = std::thread(
				[this, worker_index] {
					worker_loop(worker_index);
				}
			);
		}
	}


	void shutdown()
	{
		if (m_worker_count == 0) {
			return;
		}

		m_shutdown_requested.store(true, std::memory_order_release);

		for (uint32_t worker_index = 0; worker_index < m_worker_count; ++worker_index) {
			m_workers[worker_index].has_work.store(1, std::memory_order_release);
			m_workers[worker_index].condition.notify_one();
		}

		for (uint32_t worker_index = 0; worker_index < m_worker_count; ++worker_index) {
			if (m_worker_threads[worker_index].joinable()) {
				m_worker_threads[worker_index].join();
			}
			m_job_deques.destruct(worker_index);
			m_workers[worker_index].has_work.store(0, std::memory_order_relaxed);
		}

		m_injection_queue.reset();

		m_worker_count = 0;

		m_shutdown_requested.store(false, std::memory_order_relaxed);
		m_submit_counter.store(0, std::memory_order_relaxed);
	}


	void submit(JobLatch& job_latch, JobFn job_fn, void* job_fn_input)
	{
		HPR_ASSERT_MSG(m_worker_count > 0,
			"worker count <= 0");

		job_latch.add(1);

		const bool push_success = m_injection_queue.push(JobEntry {
			.function = job_fn,
			.fn_input = job_fn_input,
			.latch    = &job_latch
		});

		HPR_ASSERT_MSG(push_success,
			"injection ring push failed");

		const uint32_t submit_index = m_submit_counter.fetch_add(1, std::memory_order_relaxed);
		const uint32_t worker_index = submit_index % m_worker_count;

		m_workers[worker_index].has_work.store(1, std::memory_order_release);
		m_workers[worker_index].condition.notify_one();
	}


	template <typename JobInputSlice>
	void dispatch_range(
		JobLatch&      job_latch,
		JobFn          job_fn,
		uint32_t       job_input_count,
		uint32_t       job_input_grain,
		JobInputSlice* job_input_slices
	)
	{
		HPR_ASSERT_MSG(job_input_grain != 0,
			"job_input_grain == 0");

		if (job_input_count == 0 || job_input_grain == 0) {
			return;
		}

		const uint32_t job_count =
			(job_input_count + job_input_grain - 1) / job_input_grain;

		HPR_ASSERT_MSG(m_worker_count > 0,
			"worker count <= 0");

		for (uint32_t job_index = 0; job_index < job_count; ++job_index) {

			const uint32_t job_input_beg = job_index * job_input_grain;

			const uint32_t job_input_end =
				(job_input_beg + job_input_grain < job_input_count)
					? (job_input_beg + job_input_grain)
					: job_input_count;

			job_input_slices[job_index].begin = job_input_beg;
			job_input_slices[job_index].end   = job_input_end;

			job_latch.add(1);

			const bool push_success = m_injection_queue.push(JobEntry {
				.function = job_fn,
				.fn_input = &job_input_slices[job_index],
				.latch    = &job_latch
			});

			HPR_ASSERT_MSG(push_success,
				"injection ring push failed");

			const uint32_t submit_index = m_submit_counter.fetch_add(1, std::memory_order_relaxed);
			const uint32_t worker_index = submit_index % m_worker_count;

			m_workers[worker_index].has_work.store(1, std::memory_order_release);
			m_workers[worker_index].condition.notify_one();
		}
	}

private:

	void worker_loop(uint32_t worker_index)
	{
		auto& worker = m_workers[worker_index];

		for (;;) {

			JobEntry job_entry {};

			{
				if (m_job_deques[worker_index].pop_bottom(job_entry)) {
					job_entry.function(job_entry.fn_input);
					job_entry.latch->done();
					continue;
				}

				if (m_injection_queue.pop(job_entry)) {
					job_entry.function(job_entry.fn_input);
					job_entry.latch->done();
					continue;
				}

				bool stole = false;

				for (uint32_t other_index = 0; other_index < m_worker_count; ++other_index) {
					if (other_index == worker_index) {
						continue;
					}

					if (m_job_deques[other_index].steal_top(job_entry)) {
						stole = true;
						break;
					}
				}

				if (stole) {
					job_entry.function(job_entry.fn_input);
					job_entry.latch->done();
					continue;
				}

				{
					std::unique_lock<std::mutex> lock(worker.mutex);
					worker.condition.wait(lock,
						[this, &worker] {
							return m_shutdown_requested.load(std::memory_order_relaxed) ||
								worker.has_work.load(std::memory_order_acquire) != 0;
						}
					);

					worker.has_work.store(0, std::memory_order_relaxed);

					if (m_shutdown_requested.load(std::memory_order_relaxed)) {
						return;
					}
				}

				continue;
			}
		}
	}

private:

	uint32_t m_worker_count {0};

	std::atomic<uint32_t> m_submit_counter     {0};
	std::atomic<bool>     m_shutdown_requested {false};

	std::array<Worker,      cfg::max_workers> m_workers;
	std::array<std::thread, cfg::max_workers> m_worker_threads;

	mtp::crib<mtp::chaselev<JobEntry, mtp_job_set>, cfg::max_workers> m_job_deques;

	MpmcRing<JobEntry, cfg::injection_capacity> m_injection_queue;

	mtp::shared<mtp_job_set> m_metapool;
};


} // hpr::job

