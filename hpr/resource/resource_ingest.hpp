#pragma once


#include "hprint.hpp"

#include "cgltf.h"


namespace hpr::res {


enum class AttrFormat : uint8_t
{
	undefined,
	float32,
	float16,
	uint32,
	uint16,
	uint8,
};


enum class AttrType : uint8_t
{
	pos,
	nrm,
	tan,
	uv0,
	uv1,
	rgb,
	jnt,
	wgt,
	count
};



constexpr uint32_t get_format_size(AttrFormat format)
{
	switch (format) {
		case AttrFormat::float32: return 4;
		case AttrFormat::uint32:  return 4;
		case AttrFormat::float16: return 2;
		case AttrFormat::uint16:  return 2;
		case AttrFormat::uint8:   return 1;
		default:                  return 0;
	}
}

constexpr AttrFormat get_standard_format(AttrType type)
{
	switch (type) {
		case AttrType::pos:
		case AttrType::nrm:
		case AttrType::tan:
		case AttrType::uv0:
		case AttrType::uv1:
		case AttrType::rgb: return AttrFormat::float32;
		case AttrType::jnt:
		case AttrType::wgt: return AttrFormat::uint8;
		default:            return AttrFormat::undefined;
	}
}

constexpr uint8_t get_standard_components(AttrType type)
{
	switch (type) {
		case AttrType::pos: return 3;
		case AttrType::nrm: return 3;
		case AttrType::tan: return 4;
		case AttrType::uv0:
		case AttrType::uv1: return 2;
		case AttrType::rgb: return 4;
		case AttrType::jnt: return 4;
		case AttrType::wgt: return 4;
		default:            return 0;
	}
}

constexpr AttrType get_standard_attr_type(const cgltf_attribute& attr)
{
	switch (attr.type) {
		case cgltf_attribute_type_position: return AttrType::pos;
		case cgltf_attribute_type_normal:   return AttrType::nrm;
		case cgltf_attribute_type_tangent:  return AttrType::tan;
		case cgltf_attribute_type_color:    return AttrType::rgb;
		case cgltf_attribute_type_joints:   return AttrType::jnt;
		case cgltf_attribute_type_weights:  return AttrType::wgt;
		case cgltf_attribute_type_texcoord:
			if (attr.index == 0)            return AttrType::uv0;
			if (attr.index == 1)            return AttrType::uv1;
			[[fallthrough]];
		default:                            return AttrType::count;
	}
}

} // hpr::res
