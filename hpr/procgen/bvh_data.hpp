#pragma once

#include "hprint.hpp"
#include "math.hpp"


namespace hpr::geo {


namespace cfg {

inline constexpr uint32_t blas_null_lane = 255U;

} // hpr::geo::cfg


struct alignas(32) TLBVH2Node
{
	vec3     min;
	uint32_t child_idx;
	vec3     max;
	uint32_t mesh_cnt;
};

static_assert(sizeof(TLBVH2Node) == 32,
	"BVH2 TLAS node must be 32 bytes");


struct alignas(64) TLBVH8Node
{
	float x_min[8];
	float y_min[8];
	float z_min[8];

	float x_max[8];
	float y_max[8];
	float z_max[8];

	uint32_t blas_first[8];
	uint32_t blas_count[8];
};

static_assert(sizeof(TLBVH8Node) == 256,
	"BVH8 TLAS node must be 256 bytes");


struct alignas(32) BLBVH2Node
{
	vec3     min;
	uint32_t child_idx;
	vec3     max;
	uint32_t tris_cnt;
};

static_assert(sizeof(BLBVH2Node) == 32,
	"BVH2 BLAS node must be 32 bytes");


struct alignas(128) BLBVH8Node
{
	vec3  min;
	float pad0;
	vec3  max;
	float pad1;

	uint8_t x_min[8];
	uint8_t y_min[8];
	uint8_t z_min[8];

	uint8_t x_max[8];
	uint8_t y_max[8];
	uint8_t z_max[8];

	uint32_t base_idxs[8];
	uint8_t  tris_cnts[8];

	uint8_t pad2[8];
};

static_assert(sizeof(BLBVH8Node) == 128,
	"BVH8 BLAS node must be 128 bytes");


} // hpr::geo
