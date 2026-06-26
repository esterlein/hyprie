#pragma once

#include "hprint.hpp"
#include "panic.hpp"

#include "mtp_memory.hpp"

#include <unistd.h>
#include <sys/stat.h>

#include <string>
#include <string_view>
#include <cstring>
#include <optional>


namespace hpr::fs {


namespace cfg {

inline constexpr size_t max_path_len = 4096U;

} // hpr::fs::cfg


class Filesystem
{
public:

	static Filesystem& instance()
	{
		static Filesystem instance;
		return instance;
	}


	bool exists(const char* path)
	{
		struct stat buffer;
		return (stat(path, &buffer) == 0);
	}


	std::string resolve_path(std::string_view uri) const
	{
		size_t scheme_end = uri.find("://");
		if (scheme_end == std::string_view::npos) {
			HPR_PANIC_FMT("[filesystem] illformed scheme [%.*s]",
				static_cast<int>(uri.length()),
				uri.data()
			);
		}

		std::string_view scheme   = uri.substr(0, scheme_end);
		std::string_view filename = uri.substr(scheme_end + 3);
		const char* subdir        = subdir_for_scheme(scheme);

		char full_path[cfg::max_path_len];
		size_t root_len   = strlen(m_root_dir);
		size_t subdir_len = strlen(subdir);

		if (root_len + subdir_len + filename.length() >= cfg::max_path_len) {
			HPR_PANIC("[filesystem] path buffer overflow");
		}

		memcpy(full_path, m_root_dir, root_len);
		memcpy(full_path + root_len, subdir, subdir_len);
		memcpy(full_path + root_len + subdir_len, filename.data(), filename.length());

		full_path[root_len + subdir_len + filename.length()] = '\0';

		return std::string(full_path);
	}


	std::string resolve_dir(std::string_view uri) const
	{
		size_t scheme_end = uri.find("://");
		if (scheme_end == std::string_view::npos) {
			HPR_PANIC_FMT("[filesystem] illformed scheme [%.*s]",
				static_cast<int>(uri.length()),
				uri.data()
			);
		}

		return std::string(m_root_dir) + subdir_for_scheme(uri.substr(0, scheme_end));
	}

private:

	Filesystem()
	{
		char exe_dir[cfg::max_path_len];
		size_t len = get_self_path(exe_dir, sizeof(exe_dir));

		if (len == 0) {
			HPR_PANIC("[filesystem] failed to resolve self exe path");
		}

		char attempt[cfg::max_path_len];
		std::snprintf(attempt, sizeof(attempt), "%s/assets", exe_dir);

		if (exists(attempt)) {
			memcpy(m_root_dir, exe_dir, len + 1);
		}
		else {
			bool found = false;
			for (char* char_ptr = exe_dir + len - 1; char_ptr > exe_dir; --char_ptr) {
				if (*char_ptr == '/') {
					*char_ptr = '\0';
					std::snprintf(attempt, sizeof(attempt), "%s/assets", exe_dir);
					if (exists(attempt)) {
						size_t new_len = strlen(exe_dir);
						memcpy(m_root_dir, exe_dir, new_len + 1);
						found = true;
					}
					break;
				}
			}

			if (!found) {
				HPR_PANIC_FMT("[filesystem] could not find assets dir [exe dir: %s]", exe_dir);
			}
		}

		ensure_trailing_separator(m_root_dir, sizeof(m_root_dir));
	}


	size_t get_self_path(char* exe_path, size_t capacity)
	{
		ssize_t len = readlink("/proc/self/exe", exe_path, capacity - 1);
		if (len <= 0)
			return 0;

		exe_path[len] = '\0';

		for (char* char_ptr = exe_path + len; char_ptr > exe_path; --char_ptr) {
			if (*char_ptr == '/') {
				*char_ptr = '\0';
				return static_cast<size_t>(char_ptr - exe_path);
			}
		}

		return static_cast<size_t>(len);
	}

	const char* subdir_for_scheme(std::string_view scheme) const
	{
		if (scheme == "font")   return "assets/fonts/";
		if (scheme == "model")  return "assets/models/";
		if (scheme == "scene")  return "test_scene/";
		if (scheme == "shader") return "shaders/";

		HPR_PANIC_FMT(
			"[filesystem] unknown scheme category [%.*s]",
			static_cast<int>(scheme.length()),
			scheme.data()
		);

		return nullptr;
	}


	void ensure_trailing_separator(char* path, size_t capacity) const
	{
		if (!path || path[0] == '\0' || capacity == 0)
			return;

		size_t len = strlen(path);
		if (len > 0 && len < capacity - 1 && path[len - 1] != '/') {
			path[len] = '/';
			path[len + 1] = '\0';
		}
	}

	char m_root_dir[cfg::max_path_len];
};


inline std::optional<std::string> read_text_file(const char* uri)
{
	std::string file_path = fs::Filesystem::instance().resolve_path(uri);

	HPR_ASSERT_MSG(file_path.c_str(), "file_path == null");

	FILE* file_handle = std::fopen(file_path.c_str(), "rb");
	if (!file_handle) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[filesystem][read_text_file] fopen fail [%s]",
			file_path.c_str()
		);
		return std::nullopt;
	}
	if (std::fseek(file_handle, 0, SEEK_END) != 0) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[filesystem][read_text_file] fseek end fail [%s]",
			file_path.c_str()
		);
		std::fclose(file_handle);
		return std::nullopt;
	}
	long file_size_signed = std::ftell(file_handle);
	if (file_size_signed < 0) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[filesystem][read_text_file] ftell fail [%s]",
			file_path.c_str()
		);
		std::fclose(file_handle);
		return std::nullopt;
	}
	if (std::fseek(file_handle, 0, SEEK_SET) != 0) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[filesystem][read_text_file] fseek set fail [%s]",
			file_path.c_str()
		);
		std::fclose(file_handle);
		return std::nullopt;
	}

	size_t file_size = static_cast<size_t>(file_size_signed);
	std::string data;
	data.resize(file_size);

	size_t bytes_read = std::fread(data.data(), 1, file_size, file_handle);
	std::fclose(file_handle);

	if (bytes_read != file_size) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[filesystem][read_text_file] fread short [%s][bytes_read %zu][file_size %zu]",
			file_path.c_str(),
			bytes_read,
			file_size
		);
		return std::nullopt;
	}

	return data;
}


template<typename T = uint8_t>
inline std::optional<mtp::vault<T, mtp::default_set>> read_bin_file(const char* uri)
{
	std::string file_path = fs::Filesystem::instance().resolve_path(uri);

	FILE* file = std::fopen(file_path.c_str(), "rb");
	if (!file) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[fs][read_bin] fopen fail [%s]",
			file_path.c_str()
		);
		return std::nullopt;
	}

	std::fseek(file, 0, SEEK_END);
	size_t size = static_cast<size_t>(std::ftell(file));
	std::fseek(file, 0, SEEK_SET);

	if (size <= 0) {
		std::fclose(file);
		return std::nullopt;
	}

	mtp::vault<T, mtp::default_set> buffer;
	buffer.resize(size);

	size_t read_count = std::fread(buffer.data(), sizeof(T), size, file);
	std::fclose(file);

	if (read_count != size)
		return std::nullopt;

	return buffer;
}

} // hpr::fs
