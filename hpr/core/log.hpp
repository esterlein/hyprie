#pragma once

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstddef>
#include <mutex>


namespace hpr::log {


static constexpr size_t LOG_RING_CAPACITY = 256;


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
	core   = 0,
	render = 1,
	scene  = 2,
	asset  = 3,
	count
};


struct LogEntry
{
	LogLevel    level;
	LogCategory category;
	char        text[1024];
};


struct LogRing
{
	LogEntry entries[LOG_RING_CAPACITY];
	size_t   head;
	size_t   count;
};


struct LogState
{
	LogLevel level;
	FILE*    file;
	bool     is_stderr;
	LogRing  ring;
};


inline const char* category_name(LogCategory category)
{
	switch (category) {
	case LogCategory::core:   return "core";
	case LogCategory::render: return "render";
	case LogCategory::scene:  return "scene";
	case LogCategory::asset:  return "asset";
	default:                  return "core";
	}
}


inline LogState& state()
{
	static LogState log_state
	{
		LogLevel::info,
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


inline const char* level_name(LogLevel level)
{
	switch (level)
	{
	case LogLevel::fatal: return "FATAL";
	case LogLevel::error: return "ERROR";
	case LogLevel::warn:  return "WARN";
	case LogLevel::info:  return "INFO";
	case LogLevel::debug: return "DEBUG";
	case LogLevel::trace: return "TRACE";
	}
	return "UNKNOWN";
}


inline void initialize(LogLevel level)
{
	LogState& log_state = state();
	log_state.level     = level;
}


inline void shutdown()
{
	LogState& log_state = state();
	if (log_state.file) {
		std::fflush(log_state.file);
		std::fclose(log_state.file);
		log_state.file = nullptr;
	}
}


inline void set_level(LogLevel level)
{
	state().level = level;
}


inline LogLevel get_level()
{
	return state().level;
}


inline void enable_stderr(bool enabled)
{
	state().is_stderr = enabled;
}


inline bool is_enabled(LogLevel level)
{
	return static_cast<int>(level) <= static_cast<int>(state().level);
}


inline bool open_file(const char* path)
{
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

	log_ring.head = (log_ring.head + 1) % LOG_RING_CAPACITY;

	if (log_ring.count < LOG_RING_CAPACITY) {
		++log_ring.count;
	}
}


inline size_t copy_ring_entries(LogEntry* destination, size_t max_entries)
{
	if (!destination || max_entries == 0)
		return 0;

	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();
	LogRing&  log_ring  = log_state.ring;

	if (log_ring.count == 0)
		return 0;

	size_t msg_count = log_ring.count;
	if (msg_count > max_entries) {
		msg_count = max_entries;
	}

	size_t ring_start = (log_ring.head + LOG_RING_CAPACITY - log_ring.count) % LOG_RING_CAPACITY;

	for (size_t i = 0; i < msg_count; ++i) {
		size_t index = (ring_start + i) % LOG_RING_CAPACITY;
		destination[i] = log_ring.entries[index];
	}

	return msg_count;
}


inline void write(LogLevel level, LogCategory category, const char* fmt, ...)
{
	if (!is_enabled(level))
		return;

	thread_local char msg_buffer[1024];
	thread_local char fin_buffer[1152];

	va_list args;

	va_start(args, fmt);
	std::vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);
	va_end(args);

	const char* lvl_name = level_name(level);
	const char* cat_name = category_name(category);

	std::snprintf(fin_buffer, sizeof(fin_buffer), "[%s][%s]%s", lvl_name, cat_name, msg_buffer);

	std::lock_guard<std::mutex> lock(mutex());

	LogState& log_state = state();

	ring_push(level, category, fin_buffer);

	if (log_state.is_stderr)
		std::fprintf(stderr, "%s\n", fin_buffer);
	if (log_state.file)
		std::fprintf(log_state.file, "%s\n", fin_buffer);
}

} // hpr::log


#define HPR_FATAL(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::fatal, category, fmt, ##__VA_ARGS__); \
	} while (0)

#define HPR_ERROR(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::error, category, fmt, ##__VA_ARGS__); \
	} while (0)

#define HPR_WARN(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::warn, category, fmt, ##__VA_ARGS__); \
	} while (0)

#define HPR_INFO(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::info, category, fmt, ##__VA_ARGS__); \
	} while (0)

#define HPR_DEBUG(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::debug, category, fmt, ##__VA_ARGS__); \
	} while (0)

#define HPR_TRACE(category, fmt, ...) \
	do { \
		log::write(log::LogLevel::trace, category, fmt, ##__VA_ARGS__); \
	} while (0)

