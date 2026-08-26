#pragma once

#include "hprint.hpp"
#include "math.hpp"


namespace hpr::rdr {


#pragma pack(push, 1)
struct SceneVertex
{
	v3f32    pos;
	uint32_t nrm;
	uint32_t tan;
	v2u16    uv0;
};
#pragma pack(pop)

static_assert(sizeof(SceneVertex) == 24);


#pragma pack(push, 1)
struct AnimVertex
{
	v3f32    pos;
	uint32_t nrm;
	uint32_t tan;
	v2u16    uv0;
	uint8_t  jnt[4];
	uint8_t  wgt[4];
};
#pragma pack(pop)

static_assert(sizeof(AnimVertex) == 32);


#pragma pack(push, 1)
struct GenericVertex
{
	v3f32 pos;
	v2u16 uv;
};
#pragma pack(pop)

static_assert(sizeof(GenericVertex) == 16);


#pragma pack(push, 1)
struct BitmapVertex
{
	v2f32    pos;
	v2u16    uv;
	uint32_t rgb;
};
#pragma pack(pop)

static_assert(sizeof(BitmapVertex) == 16);


} // hpr::rdr

