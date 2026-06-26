#pragma once

#include "hprint.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "render_data.hpp"
#include "geometry_data.hpp"
#include <cstdint>


namespace hpr {


namespace rdr {

struct Mesh;

} // hpr::rdr


namespace edt {


enum class TransformSpace : uint8_t
{
	Local,
	World
};


struct GridParams
{
	vec3  minor_rgb;
	vec3  major_rgb;
	vec2  minor_vis_range_px;
	vec2  major_vis_range_px;
	
	float line_width_px;
	float cell_size;
	float y_plane;
	int   major_step_cells;
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


enum class GizmoSubmesh : uint32_t
{
	Arrow = 0,
	Ring,
	Quad,
	Cube,

	count
};


struct GizmoPrimitives
{
	std::array<geo::Geoslice, static_cast<size_t>(GizmoSubmesh::count)> geo_range {};

	GizmoStyle    style    {};
	GizmoTopology topology {};

	uint32_t vtx_base  {0};
	uint32_t idx_first {0};
};

} // edt
} // hpr

