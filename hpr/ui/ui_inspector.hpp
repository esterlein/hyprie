#pragma once

#include <algorithm>
#include "nuklear.h"
#include "math.hpp"
#include "entity.hpp"
#include "scene_data.hpp"
#include "hpr/editor/inspector_state.hpp"


namespace hpr::ui {


inline void ui_inspector(nk_context* context, void* opaque)
{
	if (!context)
		return;

	auto* state = static_cast<edt::InspectorState*>(opaque);
	if (!state)
		return;

	if (nk_begin(
		context,
		"ENTITY INSPECTOR",
		nk_rect(10.0f, 10.0f, 440.0f, 760.0f),
		NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)
	) {
		const float row_h = 28.0f;
		const bool has_selection = state->selection.entity != ecs::invalid_entity;

		nk_layout_row_dynamic(context, row_h, 1);

		if (has_selection) {
			char entity_label[64];
			snprintf(entity_label, sizeof(entity_label), "ENTITY: %u", static_cast<uint32_t>(state->selection.entity));
			nk_label(context, entity_label, NK_TEXT_LEFT);
		}
		else {
			nk_label(context, "ENTITY: none", NK_TEXT_LEFT);
		}
		nk_layout_row_dynamic(context, 6.0f, 1);

		if (nk_tree_push(context, NK_TREE_NODE, "Transform", NK_MAXIMIZED)) {
			if (has_selection) {
				scn::Transform t = state->selection.transform;

				nk_layout_row_dynamic(context, row_h, 3);
				t.position.x = nk_propertyf(context, "Pos X", -100000.0f, t.position.x, 100000.0f, 0.1f, 0.01f);
				t.position.y = nk_propertyf(context, "Pos Y", -100000.0f, t.position.y, 100000.0f, 0.1f, 0.01f);
				t.position.z = nk_propertyf(context, "Pos Z", -100000.0f, t.position.z, 100000.0f, 0.1f, 0.01f);

				nk_layout_row_dynamic(context, row_h, 4);
				t.rotation.x = nk_propertyf(context, "Rot X", -1.0f, t.rotation.x, 1.0f, 0.01f, 0.001f);
				t.rotation.y = nk_propertyf(context, "Rot Y", -1.0f, t.rotation.y, 1.0f, 0.01f, 0.001f);
				t.rotation.z = nk_propertyf(context, "Rot Z", -1.0f, t.rotation.z, 1.0f, 0.01f, 0.001f);
				t.rotation.w = nk_propertyf(context, "Rot W", -1.0f, t.rotation.w, 1.0f, 0.01f, 0.001f);

				nk_layout_row_dynamic(context, row_h, 3);
				t.scale.x = nk_propertyf(context, "Scl X", 0.0f, t.scale.x, 10000.0f, 0.1f, 0.01f);
				t.scale.y = nk_propertyf(context, "Scl Y", 0.0f, t.scale.y, 10000.0f, 0.1f, 0.01f);
				t.scale.z = nk_propertyf(context, "Scl Z", 0.0f, t.scale.z, 10000.0f, 0.1f, 0.01f);

				if (t.position != state->selection.transform.position ||
				    t.rotation != state->selection.transform.rotation ||
				    t.scale    != state->selection.transform.scale) {
					state->selection.transform = t;
					state->transform_dirty = true;
				}
			}
			else {
				nk_layout_row_dynamic(context, row_h, 1);
				nk_label(context, "No selection", NK_TEXT_LEFT);
			}
			nk_tree_pop(context);
		}


		nk_collapse_states light_collapse = state->section_light ? NK_MAXIMIZED : NK_MINIMIZED;
		if (nk_tree_state_push(context, NK_TREE_NODE, "Light", &light_collapse)) {
			if (state->has_light && has_selection) {
				scn::SceneLight light = state->light;
		
				nk_layout_row_dynamic(context, row_h, 1);
				int enabled = light.enabled ? 1 : 0;
				enabled = nk_check_label(context, "Enabled", enabled);
				light.enabled = static_cast<uint8_t>(enabled != 0 ? 1 : 0);
		
				nk_layout_row_dynamic(context, row_h, 3);
				light.color_rgb.x = nk_propertyf(context, "R", 0.0f, light.color_rgb.x, 1.0f, 0.01f, 0.001f);
				light.color_rgb.y = nk_propertyf(context, "G", 0.0f, light.color_rgb.y, 1.0f, 0.01f, 0.001f);
				light.color_rgb.z = nk_propertyf(context, "B", 0.0f, light.color_rgb.z, 1.0f, 0.01f, 0.001f);
		
				nk_layout_row_dynamic(context, row_h, 2);
				light.intensity = nk_propertyf(context, "Intensity", 0.0f, light.intensity, 100000.0f, 0.1f, 0.01f);
				light.range     = nk_propertyf(context, "Range",     0.0f, light.range,     100000.0f, 0.1f, 0.01f);
		
				nk_layout_row_dynamic(context, row_h, 2);
				light.inner_deg = nk_propertyf(context, "Inner", 0.0f, light.inner_deg, 180.0f, 0.1f, 0.01f);
				light.outer_deg = nk_propertyf(context, "Outer", 0.0f, light.outer_deg, 180.0f, 0.1f, 0.01f);
		
				if (light.enabled   != state->light.enabled   ||
				    light.color_rgb != state->light.color_rgb ||
				    light.intensity != state->light.intensity ||
				    light.range     != state->light.range     ||
				    light.inner_deg != state->light.inner_deg ||
				    light.outer_deg != state->light.outer_deg) {
					state->light = light;
					state->light_dirty = true;
				}
			} else {
				nk_layout_row_dynamic(context, row_h, 1);
				nk_color off = nk_rgba_f(0.6f, 0.6f, 0.6f, 0.6f);
				nk_style_push_color(context, &context->style.text.color, off);
				nk_label(context, "No light", NK_TEXT_LEFT);
				nk_style_pop_color(context);
			}
			nk_tree_pop(context);
		}
		state->section_light = (light_collapse == NK_MAXIMIZED);


		nk_collapse_states mat_collapse = state->section_material ? NK_MAXIMIZED : NK_MINIMIZED;
		if (nk_tree_state_push(context, NK_TREE_NODE, "Materials", &mat_collapse)) {
			if (state->has_material && has_selection && state->submesh_count > 0) {
				bool changed = false;

				nk_layout_row_dynamic(context, row_h, 2);
				int32_t submesh = static_cast<int32_t>(state->selection.submesh);
				submesh = nk_propertyi(context, "Submesh", 0, submesh, static_cast<int>(state->submesh_count - 1), 1, 1);
				uint32_t clamped = static_cast<uint32_t>(std::clamp(submesh, 0, static_cast<int>(state->submesh_count - 1)));
				if (clamped != state->selection.submesh) {
					state->selection.submesh = clamped;
				}

				nk_layout_row_dynamic(context, row_h, 4);
				{
					vec4 c = state->albedo_tint;
					c.x = nk_propertyf(context, "Tint R", 0.0f, c.x, 1.0f, 0.01f, 0.001f);
					c.y = nk_propertyf(context, "Tint G", 0.0f, c.y, 1.0f, 0.01f, 0.001f);
					c.z = nk_propertyf(context, "Tint B", 0.0f, c.z, 1.0f, 0.01f, 0.001f);
					c.w = nk_propertyf(context, "Tint A", 0.0f, c.w, 1.0f, 0.01f, 0.001f);
					if (c != state->albedo_tint) {
						state->albedo_tint = c;
						changed = true;
					}
				}

				nk_layout_row_dynamic(context, row_h, 2);
				{
					float m = nk_propertyf(context, "Metallic", 0.0f, state->metallic, 1.0f, 0.01f, 0.001f);
					float r = nk_propertyf(context, "Roughness", 0.0f, state->roughness, 1.0f, 0.01f, 0.001f);
					if (m != state->metallic) {
						state->metallic = m;
						changed = true;
					}
					if (r != state->roughness) {
						state->roughness = r;
						changed = true;
					}
				}

				if (state->has_normal) {
					nk_layout_row_dynamic(context, row_h, 1);
					float ns = nk_propertyf(context, "Normal Scale", -4.0f, state->normal_scale, 4.0f, 0.01f, 0.001f);
					if (ns != state->normal_scale) {
						state->normal_scale = ns;
						changed = true;
					}
				} else {
					nk_layout_row_dynamic(context, row_h, 1);
					nk_color off = nk_rgba_f(0.6f, 0.6f, 0.6f, 0.6f);
					nk_style_push_color(context, &context->style.text.color, off);
					nk_label(context, "No normal map", NK_TEXT_LEFT);
					nk_style_pop_color(context);
				}

				if (state->has_ormh) {
					nk_layout_row_dynamic(context, row_h, 1);
					float ao = nk_propertyf(context, "AO Strength", 0.0f, state->ao_strength, 1.0f, 0.01f, 0.001f);
					if (ao != state->ao_strength) {
						state->ao_strength = ao;
						changed = true;
					}
				}
				else {
					nk_layout_row_dynamic(context, row_h, 1);
					nk_color off = nk_rgba_f(0.6f, 0.6f, 0.6f, 0.6f);
					nk_style_push_color(context, &context->style.text.color, off);
					nk_label(context, "No ORMH map", NK_TEXT_LEFT);
					nk_style_pop_color(context);
				}

				if (state->has_emissive) {
					nk_layout_row_dynamic(context, row_h, 3);
					vec3 e = state->emissive;
					e.x = nk_propertyf(context, "Emissive R", 0.0f, e.x, 10.0f, 0.01f, 0.001f);
					e.y = nk_propertyf(context, "Emissive G", 0.0f, e.y, 10.0f, 0.01f, 0.001f);
					e.z = nk_propertyf(context, "Emissive B", 0.0f, e.z, 10.0f, 0.01f, 0.001f);
					if (e != state->emissive) {
						state->emissive = e;
						changed = true;
					}
				}
				else {
					nk_layout_row_dynamic(context, row_h, 1);
					nk_color off = nk_rgba_f(0.6f, 0.6f, 0.6f, 0.6f);
					nk_style_push_color(context, &context->style.text.color, off);
					nk_label(context, "No emissive map", NK_TEXT_LEFT);
					nk_style_pop_color(context);
				}

				nk_layout_row_dynamic(context, row_h, 2);
				{
					vec2 s = state->uv_scale;
					s.x = nk_propertyf(context, "UV Scale X", 0.01f, s.x, 10.0f, 0.01f, 0.001f);
					s.y = nk_propertyf(context, "UV Scale Y", 0.01f, s.y, 10.0f, 0.01f, 0.001f);
					if (s != state->uv_scale) {
						state->uv_scale = s;
						changed = true;
					}
				}

				nk_layout_row_dynamic(context, row_h, 2);
				{
					vec2 o = state->uv_offset;
					o.x = nk_propertyf(context, "UV Off X", -10.0f, o.x, 10.0f, 0.01f, 0.001f);
					o.y = nk_propertyf(context, "UV Off Y", -10.0f, o.y, 10.0f, 0.01f, 0.001f);
					if (o != state->uv_offset) {
						state->uv_offset = o;
						changed = true;
					}
				}

				if (changed) {
					state->material_dirty = true;
				}
			}
			else {
				nk_layout_row_dynamic(context, row_h, 1);
				nk_color off = nk_rgba_f(0.6f, 0.6f, 0.6f, 0.6f);
				nk_style_push_color(context, &context->style.text.color, off);
				nk_label(context, "No material", NK_TEXT_LEFT);
				nk_style_pop_color(context);
			}

			nk_tree_pop(context);
		}
		state->section_material = (mat_collapse == NK_MAXIMIZED);
	}
	nk_end(context);
}

} // hpr::ui

