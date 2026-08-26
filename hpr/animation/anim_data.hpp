#pragma once

#include "hprint.hpp"

#include "math.hpp"


namespace hpr::anm {


namespace cfg {

inline constexpr uint32_t max_skeleton_bones = 64U;

} // hpr::anm::cfg


#pragma pack(push, 1)

struct Skeleton
{
	uint32_t bone_count;
	uint32_t parent_idx_first;
	uint32_t bind_mtx_inv_first;
	uint32_t rest_mtx_L_first;
};


struct AnimClip
{
	uint32_t duration_ticks;
	uint32_t track_count;
	uint32_t track_first;
};


struct AnimTrack
{
	uint32_t bone_idx;
	uint32_t key_count;
	uint32_t key_time_first;
	uint32_t key_tsl_first;
	uint32_t key_rot_first;
};

#pragma pack(pop)


} // hpr::anm
