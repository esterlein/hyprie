#pragma once

#include <cstdint>
#include <algorithm>

#include "panic.hpp"
#include "mtp_memory.hpp"

#include "storey_data.hpp"


namespace hpr::scn {


struct Stratum
{
	mtp::vault<Storey,  mtp::default_set> storeys;
	mtp::vault<int32_t, mtp::default_set> strata_voxel_y;

	void clear()
	{
		storeys.clear();
		strata_voxel_y.clear();
	}

	[[nodiscard]] const Storey* find_storey(int32_t storey_stack, int32_t storey_index) const
	{
		// TODO: replace linear traversal

		for (const Storey& storey : storeys) {
			if (storey.stack_id == storey_stack && storey.index_y == storey_index) {
				return &storey;
			}
		}
		return nullptr;
	}

	void rebuild(const mtp::vault<StoreyStackSpec, mtp::default_set>& storey_stack_specs)
	{
		clear();

		uint32_t total_storeys = 0;
		for (const StoreyStackSpec& storey_stack_spec : storey_stack_specs) {
			total_storeys += static_cast<uint32_t>(storey_stack_spec.storey_specs.size());
		}

		storeys.reserve(total_storeys);
		strata_voxel_y.reserve(total_storeys * 2);

		for (const StoreyStackSpec& storey_stack_spec : storey_stack_specs) {

			int32_t storey_base_voxel_y = storey_stack_spec.base_voxel_y;

			for (uint32_t i = 0; i < storey_stack_spec.storey_specs.size(); ++i) {

				const int32_t storey_index  = storey_stack_spec.base_storey_index + static_cast<int32_t>(i);
				const int32_t storey_height = storey_stack_spec.storey_specs[i].height_voxels;

				HPR_ASSERT_MSG(storey_height > 0, "[stratum] storey_height <= 0");

				Storey storey {
					.stack_id    = storey_stack_spec.stack_id,
					.index_y     = storey_index,
					.voxel_y_beg = storey_base_voxel_y,
					.voxel_y_end = storey_base_voxel_y + storey_height
				};

				storeys.emplace_back(storey);

				strata_voxel_y.emplace_back(storey.voxel_y_beg);
				strata_voxel_y.emplace_back(storey.voxel_y_end);

				storey_base_voxel_y += storey_height;
			}
		}

		std::sort(strata_voxel_y.begin(), strata_voxel_y.end());

		uint32_t strata_dst = 0;
		for (uint32_t strata_src = 0; strata_src < strata_voxel_y.size(); ++strata_src) {
			if (strata_dst == 0 || strata_voxel_y[strata_src] != strata_voxel_y[strata_dst - 1]) {
				strata_voxel_y[strata_dst] = strata_voxel_y[strata_src];
				++strata_dst;
			}
		}
		strata_voxel_y.resize(strata_dst);
	}
};


} // hpr::scn

