#pragma once

#include "hprint.hpp"

#include <functional>


namespace hpr {


template<typename T>
struct Handle
{
	constexpr Handle() noexcept
	{}

	constexpr Handle(uint32_t index, uint32_t magic) noexcept
		: index {index}
		, magic {magic}
	{}

	~Handle() = default;

	constexpr Handle(const Handle& other) noexcept = default;
	constexpr Handle(Handle&& other) noexcept = default;
	constexpr Handle& operator=(const Handle& other) noexcept = default;
	constexpr Handle& operator=(Handle&& other) noexcept = default;

	constexpr bool operator==(const Handle& other) const noexcept = default;
	constexpr auto operator<=>(const Handle& other) const noexcept = default;

	constexpr uint32_t operator&(uint32_t mask) const noexcept
	{
		return index & mask;
	}

	constexpr uint64_t operator&(uint64_t mask) const noexcept
	{
		return static_cast<uint64_t>(index) & mask;
	}

	static constexpr Handle null() noexcept
	{
		return {0xFFFFFFFF, 0};
	}

	constexpr bool is_valid() const noexcept
	{
		return index != 0xFFFFFFFF;
	}

	uint32_t index {0xFFFFFFFF};
	uint32_t magic {0};
};


struct HandleHasher
{
	template <typename T>
	std::size_t operator()(const hpr::Handle<T>& handle) const noexcept
	{
		return std::hash<uint32_t>{}(handle.index);
	}
};

} // hpr
