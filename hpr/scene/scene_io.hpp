#pragma once

#include "hprint.hpp"

#include <cstdio>
#include <string>
#include <variant>
#include <optional>
#include <string_view>

#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 1

#include "toml.hpp"

#include "math.hpp"
#include "panic.hpp"
#include "mtp_memory.hpp"
#include "filesystem.hpp"

#include "scene_data.hpp"
#include "scene_io_data.hpp"
#include "log.hpp"


namespace hpr::scn::io {


static inline bool read_float_array_exact(const toml::table& table, const char* key, float* out_values, size_t expected_count)
{
	HPR_ASSERT_MSG(key, "key == null");
	HPR_ASSERT_MSG(out_values, "out_values == null");
	HPR_ASSERT_MSG(expected_count > 0, "expected_count == 0");

	if (auto array_node = table[key].as_array()) {
		if (array_node->size() != expected_count) {
			return false;
		}
		for (size_t element_index = 0; element_index < expected_count; ++element_index) {
			auto value_opt = (*array_node)[element_index].value<float>();
			if (!value_opt) {
				return false;
			}
			out_values[element_index] = *value_opt;
		}
		return true;
	}
	return false;
}

inline bool load_scene(const char* scene_uri, SceneDoc& out_scene_doc)
{
	HPR_ASSERT_MSG(scene_uri, "scene uri is null");

	auto text_opt = fs::read_text_file(scene_uri);

	if (!text_opt) {
		HPR_ERROR(
			log::LogCategory::scene,
			"[scene_io][load_scene] read fail [%s]",
			scene_uri
		);
		return false;
	}

	auto parse_result = toml::parse(*text_opt);
	if (!parse_result) {
		auto error_description = parse_result.error().description();
		HPR_ERROR(
			log::LogCategory::scene,
			"[scene_io][load_scene] toml parse fail [%s][err %s]",
			scene_uri,
			error_description.data()
		);
		return false;
	}

	const toml::table& root_table = parse_result.table();

	{
		vec3 ambient_values {0.0f, 0.0f, 0.0f};

		bool is_amb_ok =
			read_float_array_exact(root_table, "ambient_rgb", glm::value_ptr(ambient_values), vec3::length());
		if (!is_amb_ok && root_table.contains("ambient_rgb")) {
			HPR_WARN(
				log::LogCategory::scene,
				"[scene_io][load_scene] ambient_rgb invalid [%s][expected %zu]",
				scene_uri,
				vec3::length()
			);
		}

		out_scene_doc.ambient_rgb = ambient_values;
	}

	if (auto env_opt = root_table["environment"].value<std::string>()) {
		out_scene_doc.environment = fs::Filesystem::instance().resolve_dir(scene_uri) + *env_opt;
	}
	else {
		HPR_WARN(
			log::LogCategory::scene,
			"[scene_io][load_scene] missing scene environment [%s]",
			scene_uri
		);
	}

	if (auto entity_array = root_table["entity"].as_array()) {
		size_t entity_index = 0;

		for (const toml::node& entity_node : *entity_array) {
			const toml::table* entity_table = entity_node.as_table();
			if (!entity_table) {
				HPR_WARN(
					log::LogCategory::scene,
					"[scene_io][load_scene] entity not a table [%s][index %zu]",
					scene_uri,
					entity_index
				);
				++entity_index;
				continue;
			}

			EntityDoc& entity_doc = out_scene_doc.entity_docs.emplace_back();

			if (auto guid_opt = (*entity_table)["guid"].value<std::string>()) {
				entity_doc.guid = *guid_opt;
			}
			else {
				HPR_WARN(
					log::LogCategory::scene,
					"[scene_io][load_scene] entity missing guid [%s][index %zu]",
					scene_uri,
					entity_index
				);
			}

			if (auto name_opt = (*entity_table)["name"].value<std::string>()) {
				entity_doc.name = *name_opt;
			}
			else {
				HPR_WARN(
					log::LogCategory::scene,
					"[scene_io][load_scene] entity missing name [%s][index %zu]",
					scene_uri,
					entity_index
				);
			}

			if (auto parent_guid_opt = (*entity_table)["parent_guid"].value<std::string>()) {
				entity_doc.parent_guid = *parent_guid_opt;
			}

			if (auto transform_table = (*entity_table)["transform"].as_table()) {
				vec3 position_values {0.0f, 0.0f, 0.0f};
				vec4 rotation_values {0.0f, 0.0f, 0.0f, 1.0f};
				vec3 scale_values    {1.0f, 1.0f, 1.0f};

				bool is_pos_ok = read_float_array_exact(*transform_table, "position", glm::value_ptr(position_values), vec3::length());
				bool is_rot_ok = read_float_array_exact(*transform_table, "rotation", glm::value_ptr(rotation_values), vec4::length());
				bool is_scl_ok = read_float_array_exact(*transform_table, "scale",    glm::value_ptr(scale_values),    vec3::length());

				if (!is_pos_ok && transform_table->contains("position")) {
					HPR_WARN(
						log::LogCategory::scene,
						"[scene_io][load_scene] transform position invalid [%s][entity %zu][expected %zu]",
						scene_uri,
						entity_index,
						vec3::length()
					);
				}
				if (!is_rot_ok && transform_table->contains("rotation")) {
					HPR_WARN(
						log::LogCategory::scene,
						"[scene_io][load_scene] transform rotation invalid [%s][entity %zu][expected %zu]",
						scene_uri,
						entity_index,
						vec4::length()
					);
				}
				if (!is_scl_ok && transform_table->contains("scale")) {
					HPR_WARN(
						log::LogCategory::scene,
						"[scene_io][load_scene] transform scale invalid [%s][entity %zu][expected %zu]",
						scene_uri,
						entity_index,
						vec3::length()
					);
				}

				TransformDoc transform_doc {};
				transform_doc.position = position_values;
				transform_doc.rotation = quat(rotation_values.w, rotation_values.x, rotation_values.y, rotation_values.z);
				transform_doc.scale    = scale_values;

				ComponentDoc transform_component {};
				transform_component.kind    = ComponentDoc::ComponentKind::Transform;
				transform_component.payload = transform_doc;
				entity_doc.components.emplace_back(transform_component);
			}

			if (auto model_table = (*entity_table)["model"].as_table()) {
				ModelDoc model_doc {};
				auto gltf_opt = (*model_table)["gltf"].value<std::string>();

				if (gltf_opt) {
					model_doc.gltf_path = fs::Filesystem::instance().resolve_dir(scene_uri) + *gltf_opt;

					ComponentDoc model_component {};
					model_component.kind    = ComponentDoc::ComponentKind::Model;
					model_component.payload = model_doc;
					entity_doc.components.emplace_back(model_component);
				}
				else {
					HPR_ERROR(
						log::LogCategory::scene,
						"[scene_io][load_scene] model missing gltf filename [%s][entity %zu]",
						scene_uri,
						entity_index
					);
				}
			}

			if (auto camera_table = (*entity_table)["camera"].as_table()) {
				CameraDoc camera_doc {};

				if (auto fov_opt = (*camera_table)["fov_deg"].value<float>()) {
					camera_doc.fov_deg = *fov_opt;
				}
				if (auto znear_opt = (*camera_table)["znear"].value<float>()) {
					camera_doc.znear = *znear_opt;
				}
				if (auto zfar_opt = (*camera_table)["zfar"].value<float>()) {
					camera_doc.zfar = *zfar_opt;
				}
				if (auto active_opt = (*camera_table)["active"].value<bool>()) {
					camera_doc.active = *active_opt;
				}

				ComponentDoc camera_component {};
				camera_component.kind    = ComponentDoc::ComponentKind::Camera;
				camera_component.payload = camera_doc;
				entity_doc.components.emplace_back(camera_component);
			}

			if (auto light_table = (*entity_table)["light"].as_table()) {
				LightDoc light_doc {};

				int light_type_int = 0;
				if (auto type_opt = (*light_table)["type"].value<int>()) {
					light_type_int = *type_opt;
				}
				light_doc.type = static_cast<LightType>(light_type_int);

				vec3 color_values {1.0f, 1.0f, 1.0f};

				bool is_color_ok = read_float_array_exact(*light_table, "color_rgb", glm::value_ptr(color_values), vec3::length());
				if (!is_color_ok && light_table->contains("color_rgb")) {
					HPR_WARN(
						log::LogCategory::scene,
						"[scene_io][load_scene] light color_rgb invalid [%s][entity %zu][expected %zu]",
						scene_uri,
						entity_index,
						vec3::length()
					);
				}
				light_doc.color_rgb = color_values;

				if (auto intensity_opt = (*light_table)["intensity"].value<float>()) {
					light_doc.intensity = *intensity_opt;
				}
				if (auto range_opt = (*light_table)["range"].value<float>()) {
					light_doc.range = *range_opt;
				}
				if (auto inner_deg_opt = (*light_table)["inner_deg"].value<float>()) {
					light_doc.inner_deg = *inner_deg_opt;
				}
				if (auto outer_deg_opt = (*light_table)["outer_deg"].value<float>()) {
					light_doc.outer_deg = *outer_deg_opt;
				}
				if (auto enabled_opt = (*light_table)["enabled"].value<bool>()) {
					light_doc.enabled = *enabled_opt;
				}

				ComponentDoc light_component {};
				light_component.kind    = ComponentDoc::ComponentKind::Light;
				light_component.payload = light_doc;
				entity_doc.components.emplace_back(light_component);
			}

			++entity_index;
		}
	}

	HPR_INFO(
		log::LogCategory::scene,
		"[scene_io][load_scene] ok [%s][entities %zu]",
		scene_uri,
		out_scene_doc.entity_docs.size()
	);

	return true;
}

} // hpr::scn::io

