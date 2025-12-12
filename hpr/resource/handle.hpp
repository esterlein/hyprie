#pragma once

#include <cstdint>


namespace hpr {


template<typename T>
struct Handle
{
	constexpr Handle() noexcept
	{}

	constexpr Handle(uint32_t index, uint32_t magic) noexcept
		: index(index)
		, magic(magic)
	{}

	~Handle() = default;

	constexpr Handle(const Handle& other) noexcept = default;
	constexpr Handle(Handle&& other) noexcept = default;
	constexpr Handle& operator=(const Handle& other) noexcept = default;
	constexpr Handle& operator=(Handle&& other) noexcept = default;

	constexpr bool operator==(const Handle& other) const noexcept
	{
		return index == other.index && magic == other.magic;
	}

	constexpr bool operator!=(const Handle& other) const noexcept
	{
		return !(*this == other);
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

} // hpr
