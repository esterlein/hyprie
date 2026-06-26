#pragma once

#include "hprint.hpp"

#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdarg>


namespace hpr::log {


namespace cfg {

inline constexpr size_t log_ring_capacity = 1024U;
inline constexpr size_t log_entry_length  = 256U;

} // hpr::log::cfg


enum class LogLevel : uint8_t
{
	fatal = 0,
	error,
	warn,
	info,
	debug,
	trace
};

enum class LogCategory : uint8_t
{
	core = 0,
	render,
	scene,
	asset,
	procgen,
	count
};

struct LogEntry
{
	LogLevel    level;
	LogCategory category;
	char        text[cfg::log_entry_length];
};

struct LogRing
{
	LogEntry entries[cfg::log_ring_capacity];
	size_t   head;
	size_t   count;
};

struct LogState
{
	std::atomic<uint8_t> level;
	FILE*                file;
	std::atomic<bool>    is_stderr;
	LogRing              ring;
};

inline const char* category_name(LogCategory category)
{
	switch (category) {
	case LogCategory::core:   return "core";
	case LogCategory::render: return "render";
	case LogCategory::scene:  return "scene";
	case LogCategory::asset:  return "asset";
	default:                  return "unknown";
	}
}

inline const char* level_name(LogLevel level)
{
	switch (level) {
	case LogLevel::fatal: return "FATAL";
	case LogLevel::error: return "ERROR";
	case LogLevel::warn:  return "WARN";
	case LogLevel::info:  return "INFO";
	case LogLevel::debug: return "DEBUG";
	case LogLevel::trace: return "TRACE";
	}
	return "UNKNOWN";
}

inline LogState& state()
{
	static LogState log_state
	{
		static_cast<uint8_t>(LogLevel::info),
		nullptr,
		true,
		{{}, 0, 0}
	};

	return log_state;
}

inline std::mutex& mutex()
{
	static std::mutex mut;
	return mut;
}

inline void initialize(LogLevel level)
{
	state().level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

inline void shutdown()
{
	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();

	if (log_state.file) {
		std::fflush(log_state.file);
		std::fclose(log_state.file);
		log_state.file = nullptr;
	}
}

inline void set_level(LogLevel level)
{
	state().level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

inline LogLevel get_level()
{
	return static_cast<LogLevel>(state().level.load(std::memory_order_relaxed));
}

inline void enable_stderr(bool enabled)
{
	state().is_stderr.store(enabled, std::memory_order_relaxed);
}

inline bool is_enabled(LogLevel level)
{
	const uint8_t cur = state().level.load(std::memory_order_relaxed);
	return static_cast<uint8_t>(level) <= cur;
}

inline bool open_file(const char* path)
{
	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();

	if (log_state.file) {
		std::fflush(log_state.file);
		std::fclose(log_state.file);
		log_state.file = nullptr;
	}

	log_state.file = std::fopen(path, "w");
	return log_state.file != nullptr;
}

inline void ring_push(LogLevel level, LogCategory category, const char* text)
{
	LogState& log_state = state();
	LogRing&  log_ring  = log_state.ring;

	LogEntry& entry = log_ring.entries[log_ring.head];

	entry.level    = level;
	entry.category = category;

	std::snprintf(entry.text, sizeof(entry.text), "%s", text);

	log_ring.head = (log_ring.head + 1) % cfg::log_ring_capacity;

	if (log_ring.count < cfg::log_ring_capacity) {
		++log_ring.count;
	}
}

inline size_t copy_ring_entries(LogEntry* destination, size_t max_entries)
{
	if (!destination || max_entries == 0) {
		return 0;
	}

	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();
	LogRing&  log_ring  = log_state.ring;

	if (log_ring.count == 0) {
		return 0;
	}

	size_t msg_count = log_ring.count;
	if (msg_count > max_entries) {
		msg_count = max_entries;
	}

	const size_t ring_start =
		(log_ring.head + cfg::log_ring_capacity - log_ring.count) % cfg::log_ring_capacity;

	for (size_t i = 0; i < msg_count; ++i) {
		const size_t index = (ring_start + i) % cfg::log_ring_capacity;
		destination[i] = log_ring.entries[index];
	}

	return msg_count;
}

inline void write(LogLevel level, LogCategory category, const char* format, ...)
{
	if (!is_enabled(level)) {
		return;
	}

	thread_local char msg_buffer[1024];
	thread_local char fin_buffer[1152];

	va_list args;

	va_start(args, format);
	std::vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
	va_end(args);

	const char* lvl_name = level_name(level);
	const char* cat_name = category_name(category);

	std::snprintf(fin_buffer, sizeof(fin_buffer), "[%s][%s] %s", lvl_name, cat_name, msg_buffer);

	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();

	ring_push(level, category, fin_buffer);

	if (log_state.is_stderr.load(std::memory_order_relaxed)) {
		std::fprintf(stderr, "%s\n", fin_buffer);
	}
	if (log_state.file) {
		std::fprintf(log_state.file, "%s\n", fin_buffer);
	}
}

} // hpr::log

#define HPR_FATAL(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::fatal, category, format, ##__VA_ARGS__); \
	} while (0)

#define HPR_ERROR(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::error, category, format, ##__VA_ARGS__); \
	} while (0)

#define HPR_WARN(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::warn, category, format, ##__VA_ARGS__); \
	} while (0)

#define HPR_INFO(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::info, category, format, ##__VA_ARGS__); \
	} while (0)

#define HPR_DEBUG(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::debug, category, format, ##__VA_ARGS__); \
	} while (0)

#define HPR_TRACE(category, format, ...) \
	do { \
		hpr::log::write(hpr::log::LogLevel::trace, category, format, ##__VA_ARGS__); \
	} while (0)

