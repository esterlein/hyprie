#pragma once

#include <cstdint>


namespace hpr::ecs {


using Entity = uint32_t;


namespace ctx {

inline constexpr Entity invalid_entity {0xFFFFFFFFU};

} // hpr::ecs::ctx

} // hpr::ecs
