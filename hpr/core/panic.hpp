#pragma once

#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace hpr::fail {


namespace {


	static constexpr size_t frame_width = 80;

	inline void print_frame_line()
	{
		for (size_t i = 0; i < frame_width; ++i)
			std::fputc('*', stderr);

		std::fputc('\n', stderr);
	}

	inline void print_empty_line()
	{
		std::fputc('*', stderr);

		for (size_t i = 0; i < frame_width - 2; ++i)
			std::fputc(' ', stderr);

		std::fputc('*',  stderr);
		std::fputc('\n', stderr);
	}

	inline void print_centered_line(const char* text)
	{
		if (!text || text[0] == '\0')
			return;

		size_t len = std::strlen(text);
		const size_t max_len = frame_width - 4;
		if (len > max_len)
			len = max_len;

		const size_t content_width = len + 2;
		const size_t pad_total     = frame_width - 2 - content_width;
		const size_t pad_left      = pad_total / 2;
		const size_t pad_right     = pad_total - pad_left;

		std::fprintf(stderr, "*%*s %.*s %*s*\n",
			static_cast<int>(pad_left), "",
			static_cast<int>(len), text,
			static_cast<int>(pad_right), ""
		);
	}
}


[[noreturn]] inline void panic(const char* message, const char* file, int line)
{
	HPR_FATAL(log::LogCategory::core, "%s [%s:%d]", message, file, line);

	thread_local char location_buffer[256];
	std::snprintf(location_buffer, sizeof(location_buffer), "at %s:%d", file, line);

	std::fprintf(stderr, "\n");

	print_frame_line();
	print_empty_line();
	print_centered_line("HYPRIE PANIC");
	print_empty_line();
	print_centered_line(message);
	print_centered_line(location_buffer);
	print_empty_line();
	print_frame_line();

	std::fprintf(stderr, "\n");
	std::fflush(stderr);

	std::abort();
}


[[noreturn]] inline void panic_fmt(const char* file, int line, const char* fmt, ...)
{
	thread_local char msg_buffer[1024];

	va_list args;
	va_start(args, fmt);
	std::vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);
	va_end(args);

	panic(msg_buffer, file, line);
}


} // hpr::fail


#define HPR_PANIC(message) \
	::hpr::fail::panic(message, __FILE__, __LINE__)

#define HPR_PANIC_FMT(fmt, ...) \
	::hpr::fail::panic_fmt(__FILE__, __LINE__, fmt, ##__VA_ARGS__)


#ifndef NDEBUG

#define HPR_ASSERT(expr) \
	do { \
		if (!(expr)) { \
			::hpr::fail::panic("assertion failed: " #expr, __FILE__, __LINE__); \
		} \
	} while (0)

#define HPR_ASSERT_MSG(expr, message) \
	do { \
		if (!(expr)) { \
			::hpr::fail::panic(message, __FILE__, __LINE__); \
		} \
	} while (0)

#else

#define HPR_ASSERT(expr) ((void)0)
#define HPR_ASSERT_MSG(expr, message) ((void)0)

#endif

