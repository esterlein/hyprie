#pragma once

#include "hprint.hpp"


namespace hpr::geo {


struct Geoslice
{
	uint32_t vtx_base  {0};
	uint32_t vtx_count {0};
	uint32_t idx_first {0};
	uint32_t idx_count {0};

	inline bool is_valid() const { return idx_count > 0; }
};

} // hpr::geo
