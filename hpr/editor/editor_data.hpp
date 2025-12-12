#pragma once

#include <cstdint>
#include "math.hpp"
#include "handle.hpp"


namespace hpr {
namespace rdr {

struct Program;
struct Mesh;

} // rdr


namespace edt {


enum class TransformSpace : uint8_t
{
	Local,
	World
};


struct GeometryRange
{
	uint32_t first_idx {0};
	uint32_t idx_count {0};
	uint32_t first_vtx {0};
	uint32_t vtx_count {0};
};


struct GridParams
{
	vec4  plane;
	vec3  color_minor_rgb;
	float base_spacing;
	vec3  color_major_rgb;
	float target_px;
	float line_width_px;
	int   major_step;
};


enum class GizmoMode
{
	None = 0,
	Translate,
	Rotate,
	Scale
};


enum class GizmoAxis : uint8_t
{
	None = 0,
	X,
	Y,
	Z,
	XY,
	XZ,
	YZ,
	Screen,
	All
};


struct GizmoTopology
{
	uint32_t ring_segments;
	uint32_t cone_segments;
};


struct GizmoStyle
{
	float axis_len_px;
	float axis_thick_px;
	float cone_len_px;
	float cone_rad_px;
	float plane_side_px;
	float ring_radius_px;
	float ring_thick_px;
	float ring_height_px;
	float tip_cube_px;
	float alpha_plane;
	float alpha_ring;
	float alpha_axis;
};


struct GizmoPrimitives
{
	Handle<rdr::Mesh> mesh;

	uint32_t submesh_arrow;
	uint32_t submesh_ring;
	uint32_t submesh_quad;
	uint32_t submesh_cube;

	Handle<rdr::Program> program;

	GizmoTopology topology;
	uint64_t      topology_key;

	GizmoStyle style;
};

} // edt
} // hpr

