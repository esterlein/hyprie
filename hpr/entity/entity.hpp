#pragma once

#include <cstdint>


namespace hpr::ecs {


using Entity = uint32_t;

inline constexpr Entity invalid_entity {0xFFFFFFFFU};

} // hpr::ecs
