#include "scene.hpp"

namespace hpr::scn {


void Scene::clear()
{
	m_guidmap.clear();
	m_mat_templates.clear();
	m_scene_primitives.clear();

	m_ambient_rgb = {0.0f, 0.0f, 0.0f};
}

void Scene::index(ecs::Entity entity, uint64_t guid)
{
	m_guidmap[guid] = entity;
}

ecs::Entity Scene::find(uint64_t guid) const
{
	auto it = m_guidmap.find(guid);
	return it == m_guidmap.end() ? ecs::invalid_entity : it->second;
}

const Scene::GuidMap& Scene::map() const
{
	return m_guidmap;
}

void Scene::set_ambient(const vec3 rgb) noexcept
{
	m_ambient_rgb = rgb;
}

vec3 Scene::ambient() const noexcept
{
	return m_ambient_rgb;
}

void Scene::bind_material_template(uint32_t id, Handle<res::MaterialResource> handle)
{
	m_mat_templates[id] = handle;
}

Handle<res::MaterialResource> Scene::resolve_material_template(uint32_t id) const
{
	auto it = m_mat_templates.find(id);
	return it == m_mat_templates.end() ? Handle<res::MaterialResource>::null() : it->second;
}

} // hpr::scn

