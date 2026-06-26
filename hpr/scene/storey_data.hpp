#pragma once

#include "mtp_memory.hpp"


namespace hpr::scn {


struct Storey
{
	int32_t stack_id {0};
	int32_t index_y  {0};

	int32_t voxel_y_beg {0};
	int32_t voxel_y_end {0};
};


struct StoreySpec
{
	int32_t height_voxels {0};
};


struct StoreyStackSpec
{
	int32_t stack_id {0};
	int32_t base_voxel_y {0};

	int32_t base_storey_index {0};

	mtp::vault<StoreySpec, mtp::default_set> storey_specs;
};


} // hpr::scn
