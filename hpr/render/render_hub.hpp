#pragma once

#include <type_traits>

#include "handle.hpp"
#include "handle_store.hpp"
#include "render_data.hpp"
#include "tile_draw_data.hpp"


namespace hpr::rdr {


template<typename T>
concept supported_resource =
	std::is_same_v<T, Mesh>             ||
	std::is_same_v<T, MeshGeometry>     ||
	std::is_same_v<T, Texture>          ||
	std::is_same_v<T, Program>          ||
	std::is_same_v<T, MaterialTemplate> ||
	std::is_same_v<T, MaterialInstance> ||
	std::is_same_v<T, TileStyle>         ||
	std::is_same_v<T, FontTexture>;


class RenderHub
{
public:

	RenderHub()
		: m_mesh_store          {1024}
		, m_mesh_geometry_store {1024}
		, m_texture_store       {2048}
		, m_program_store       {64}
		, m_mat_template_store  {1024}
		, m_mat_instance_store  {4096}
		, m_tile_style_store     {16}
		, m_font_texture_store  {8}
	{}

	RenderHub(const RenderHub&) = delete;
	RenderHub& operator=(const RenderHub&) = delete;

	RenderHub(RenderHub&&) noexcept = default;
	RenderHub& operator=(RenderHub&&) noexcept = default;

	template<supported_resource T, typename... Types>
	Handle<T> create(Types&&... args)
	{
		return select_store<T>().create(std::forward<Types>(args)...);
	}

	template<supported_resource T>
	T* get(Handle<T> handle)
	{
		return select_store<T>().get(handle);
	}

	template<supported_resource T>
	const T* get(Handle<T> handle) const
	{
		return select_store<T>().get(handle);
	}

	template<supported_resource T>
	void destroy(Handle<T> handle)
	{
		select_store<T>().destroy(handle);
	}

	template<supported_resource T>
	auto& storage()
	{
		return select_store<T>();
	}

	template<supported_resource T>
	const auto& storage() const
	{
		return select_store<T>();
	}

private:

	template<supported_resource T>
	auto& select_store()
	{
		if constexpr (std::is_same_v<T, Mesh>)
			return m_mesh_store;
		else if constexpr (std::is_same_v<T, MeshGeometry>)
			return m_mesh_geometry_store;
		else if constexpr (std::is_same_v<T, Texture>)
			return m_texture_store;
		else if constexpr (std::is_same_v<T, Program>)
			return m_program_store;
		else if constexpr (std::is_same_v<T, MaterialTemplate>)
			return m_mat_template_store;
		else if constexpr (std::is_same_v<T, MaterialInstance>)
			return m_mat_instance_store;
		else if constexpr (std::is_same_v<T, TileStyle>)
			return m_tile_style_store;
		else if constexpr (std::is_same_v<T, FontTexture>)
			return m_font_texture_store;
	}

	template<supported_resource T>
	const auto& select_store() const
	{
		if constexpr (std::is_same_v<T, Mesh>)
			return m_mesh_store;
		else if constexpr (std::is_same_v<T, MeshGeometry>)
			return m_mesh_geometry_store;
		else if constexpr (std::is_same_v<T, Texture>)
			return m_texture_store;
		else if constexpr (std::is_same_v<T, Program>)
			return m_program_store;
		else if constexpr (std::is_same_v<T, MaterialTemplate>)
			return m_mat_template_store;
		else if constexpr (std::is_same_v<T, MaterialInstance>)
			return m_mat_instance_store;
		else if constexpr (std::is_same_v<T, TileStyle>)
			return m_tile_style_store;
		else if constexpr (std::is_same_v<T, FontTexture>)
			return m_font_texture_store;
	}

private:

	res::HandleStore<Mesh>             m_mesh_store;
	res::HandleStore<MeshGeometry>     m_mesh_geometry_store;
	res::HandleStore<Texture>          m_texture_store;
	res::HandleStore<Program>          m_program_store;
	res::HandleStore<MaterialTemplate> m_mat_template_store;
	res::HandleStore<MaterialInstance> m_mat_instance_store;
	res::HandleStore<TileStyle>        m_tile_style_store;

	res::HandleStore<FontTexture>      m_font_texture_store;
};

} // hpr::rdr

