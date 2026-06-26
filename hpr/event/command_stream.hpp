#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <type_traits>

#include "math.hpp"
#include "panic.hpp"
#include "entity.hpp"
#include "scene_data.hpp"
#include "draw_view_data.hpp"


namespace hpr {


enum class CmdType : uint8_t
{
	SetTransform = 1,
	SetLight     = 2,
	SetMaterial  = 3
};


struct CmdHeader
{
	uint16_t type;
	uint16_t flags;
	uint32_t size;
};


struct SetTransform
{
	ecs::Entity entity;
	scn::Transform transform;
};


struct SetLight
{
	ecs::Entity entity;
	scn::SceneLight light;
};


struct SetMaterial
{
	ecs::Entity entity;

	uint32_t submesh;

	vec4 albedo_tint;

	float metallic_factor;
	float roughness_factor;
	float normal_scale;
	float ao_strength;

	vec3 emissive_factor;
	vec2 uv_scale;
	vec2 uv_offset;
};


struct CmdStream
{
	uint8_t* data     {nullptr};
	uint32_t capacity {0};
	uint32_t size     {0};

	void set_storage(void* storage, uint32_t capacity_in)
	{
		data     = static_cast<uint8_t*>(storage);
		capacity = capacity_in;
		size     = 0;
	}

	void reset()
	{
		size = 0;
	}

	template <typename T>
	bool push(CmdType type, const T& payload)
	{
		static_assert(std::is_trivially_copyable_v<T>);

		const uint32_t payload_size = static_cast<uint32_t>(sizeof(T));
		const uint32_t total_size   = static_cast<uint32_t>(sizeof(CmdHeader) + payload_size);
		const uint32_t aligned_size = (total_size + 7U) & ~7U;

		if (!data || size + aligned_size > capacity)
			return false;

		CmdHeader header;
		header.type  = static_cast<uint16_t>(type);
		header.flags = 0;
		header.size  = aligned_size;

		std::memcpy(data + size, &header, sizeof(CmdHeader));
		std::memcpy(data + size + sizeof(CmdHeader), &payload, sizeof(T));

		size += aligned_size;
		return true;
	}

	struct Reader
	{
		const uint8_t* cursor {nullptr};
		const uint8_t* limit  {nullptr};

		void begin(const CmdStream& command_stream)
		{
			cursor = command_stream.data;
			limit  = command_stream.data ? command_stream.data + command_stream.size : nullptr;

			HPR_ASSERT((command_stream.size % 8U) == 0U);
		}

		bool next(CmdType& type, const void*& payload, uint32_t& payload_size)
		{
			if (!cursor || cursor >= limit)
				return false;

			const CmdHeader* header = reinterpret_cast<const CmdHeader*>(cursor);

			HPR_ASSERT(header->size >= sizeof(CmdHeader));
			HPR_ASSERT(cursor + header->size <= limit);

			type = static_cast<CmdType>(header->type);
			payload = cursor + sizeof(CmdHeader);
			payload_size = static_cast<uint32_t>(header->size) - static_cast<uint32_t>(sizeof(CmdHeader));

			cursor += header->size;
			return true;
		}
	};
};

} // hpr

