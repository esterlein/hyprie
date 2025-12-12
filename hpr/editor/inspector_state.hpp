#pragma once

#include "scene_data.hpp"
#include "render_data.hpp"


namespace hpr::edt {


struct InspectorState
{
	bool section_light    {true};
	bool section_material {true};

	scn::Selection selection {};
	uint32_t submesh_count {0};

	bool has_light {false};
	scn::SceneLight light {};

	bool has_material {false};

	bool has_albedo   {false};
	bool has_ormh     {false};
	bool has_normal   {false};
	bool has_emissive {false};

	vec4 albedo_tint   {1.0f, 1.0f, 1.0f, 1.0f};

	float metallic     {0.0f};
	float roughness    {1.0f};
	float ao_strength  {1.0f};
	float normal_scale {1.0f};

	vec3 emissive  {0.0f, 0.0f, 0.0f};

	vec2 uv_scale  {1.0f, 1.0f};
	vec2 uv_offset {0.0f, 0.0f};

	bool transform_dirty {false};
	bool light_dirty     {false};
	bool material_dirty  {false};
};

struct InspectorSnapshot
{
	uint32_t submesh_count {0};

	bool has_light {false};
	scn::SceneLight light {};

	bool has_material {false};
	Handle<rdr::MaterialInstance> material {};
};


class InspectorProvider
{
public:

	virtual ~InspectorProvider() = default;
	virtual InspectorSnapshot selection_properties() const = 0;
};

} // hpr::ui

