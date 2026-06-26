#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "render_context.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


template <typename Vertex, typename Index>
class VertexMass
{
public:

	struct VtxBlock
	{
		uint32_t vtx_base;
		uint32_t idx_first;
	};

	VertexMass(uint32_t max_vertices, uint32_t max_indices)
	{
		sg_buffer_desc vbuf_desc {};
		vbuf_desc.size                 = max_vertices * sizeof(Vertex);
		vbuf_desc.usage.vertex_buffer  = true;
		vbuf_desc.usage.dynamic_update = true;
		vbuf_desc.label                = "vtx_megabuf";
		m_vtx_buffer = sg_make_buffer(&vbuf_desc);

		sg_buffer_desc ibuf_desc {};
		ibuf_desc.size                 = max_indices * sizeof(Index);
		ibuf_desc.usage.index_buffer   = true;
		ibuf_desc.usage.dynamic_update = true;
		ibuf_desc.label                = "idx_megabuf";
		m_idx_buffer = sg_make_buffer(&ibuf_desc);
	}

	~VertexMass()
	{
		if (m_vtx_buffer.id)
			sg_destroy_buffer(m_vtx_buffer);

		if (m_idx_buffer.id)
			sg_destroy_buffer(m_idx_buffer);
	}

	VtxBlock push(
		const Vertex* vertices,
		uint32_t      vtx_count,
		const Index*  indices,
		uint32_t      idx_count
	)
	{
		uint32_t vtx_base  = static_cast<uint32_t>(m_vtx_stage.size());
		uint32_t idx_first = static_cast<uint32_t>(m_idx_stage.size());

		m_vtx_stage.resize(vtx_base + vtx_count);
		m_idx_stage.resize(idx_first + idx_count);

		std::memcpy(
			m_vtx_stage.data() + vtx_base,
			vertices,
			vtx_count * sizeof(Vertex)
		);

		std::memcpy(
			m_idx_stage.data() + idx_first,
			indices,
			idx_count * sizeof(Index)
		);

		m_dirty = true;

		return {vtx_base, idx_first};
	}

	void sync()
	{
		if (!m_dirty || m_vtx_stage.size() == 0)
			return;

		sg_range vtx_range = {m_vtx_stage.data(), m_vtx_stage.size() * sizeof(Vertex)};
		sg_range idx_range = {m_idx_stage.data(), m_idx_stage.size() * sizeof(Index)};

		sg_update_buffer(m_vtx_buffer, &vtx_range);
		sg_update_buffer(m_idx_buffer, &idx_range);

		m_dirty = false;
	}

	VertexBind bind_state() const
	{
		return VertexBind {m_vtx_buffer, m_idx_buffer};
	}

	sg_buffer vtx_buffer() const
	{
		return m_vtx_buffer;
	}

	sg_buffer idx_buffer() const
	{
		return m_idx_buffer;
	}

	const Vertex* vtx_data() const
	{
		return m_vtx_stage.data();
	}

	const Index* idx_data() const
	{
		return m_idx_stage.data();
	}

	uint32_t vtx_count() const
	{
		return static_cast<uint32_t>(m_vtx_stage.size());
	}

	uint32_t idx_count() const
	{
		return static_cast<uint32_t>(m_idx_stage.size());
	}

	void clear()
	{
		m_vtx_stage.clear();
		m_idx_stage.clear();
		m_dirty = false;
	}

private:

	mtp::vault<Vertex, mtp::default_set> m_vtx_stage;
	mtp::vault<Index,  mtp::default_set> m_idx_stage;

	sg_buffer m_vtx_buffer {};
	sg_buffer m_idx_buffer {};

	bool m_dirty {false};
};

} // hpr::rdr
