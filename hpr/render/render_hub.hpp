#pragma once

#include <type_traits>

#include "handle.hpp"
#include "handle_store.hpp"
#include "font_data.hpp"
#include "render_data.hpp"
#include "texture_data.hpp"
#include "draw_tile_data.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


template<typename T>
concept CPUResource =
	std::is_same_v<T, Mesh>             ||
	std::is_same_v<T, Texture>          ||
	std::is_same_v<T, MaterialTemplate> ||
	std::is_same_v<T, MaterialInstance> ||
	std::is_same_v<T, Font>;


class RenderHub
{
public:

	RenderHub()
		: m_mesh_store         {1024}
		, m_texture_store      {4096}
		, m_mat_template_store {1024}
		, m_mat_instance_store {4096}
		, m_font_store         {8}
	{}

	RenderHub(const RenderHub&) = delete;
	RenderHub& operator=(const RenderHub&) = delete;

	RenderHub(RenderHub&&) noexcept = default;
	RenderHub& operator=(RenderHub&&) noexcept = default;

public:

	template<CPUResource T, typename... Types>
	Handle<T> create(Types&&... args)
	{
		return select_store<T>().create(std::forward<Types>(args)...);
	}

	template<CPUResource T>
	T get_value(Handle<T> handle) const
	{
		return select_store<T>()[handle];
	}

	template<CPUResource T>
	T* get(Handle<T> handle)
	{
		return select_store<T>().get(handle);
	}

	template<CPUResource T>
	const T* get(Handle<T> handle) const
	{
		return select_store<T>().get(handle);
	}

	template<CPUResource T>
	void destroy(Handle<T> handle)
	{
		select_store<T>().destroy(handle);
	}

	template<CPUResource T>
	auto& storage()
	{
		return select_store<T>();
	}

	template<CPUResource T>
	const auto& storage() const
	{
		return select_store<T>();
	}

private:

	template<CPUResource T>
	auto& select_store()
	{
		if constexpr (std::is_same_v<T, Mesh>)
			return m_mesh_store;
		else if constexpr (std::is_same_v<T, Texture>)
			return m_texture_store;
		else if constexpr (std::is_same_v<T, MaterialTemplate>)
			return m_mat_template_store;
		else if constexpr (std::is_same_v<T, MaterialInstance>)
			return m_mat_instance_store;
		else if constexpr (std::is_same_v<T, Font>)
			return m_font_store;
	}

	template<CPUResource T>
	const auto& select_store() const
	{
		if constexpr (std::is_same_v<T, Mesh>)
			return m_mesh_store;
		else if constexpr (std::is_same_v<T, Texture>)
			return m_texture_store;
		else if constexpr (std::is_same_v<T, MaterialTemplate>)
			return m_mat_template_store;
		else if constexpr (std::is_same_v<T, MaterialInstance>)
			return m_mat_instance_store;
		else if constexpr (std::is_same_v<T, Font>)
			return m_font_store;
	}

private:

	res::HandleStore<Mesh>             m_mesh_store;
	res::HandleStore<Texture>          m_texture_store;
	res::HandleStore<MaterialTemplate> m_mat_template_store;
	res::HandleStore<MaterialInstance> m_mat_instance_store;
	res::HandleStore<Font>             m_font_store;
};


} // hpr::rdr
