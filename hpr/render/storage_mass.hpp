#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "render_context.hpp"

#include "sokol_gfx.h"

#include <cstring>


namespace hpr::rdr {


template <typename T>
class StorageMass
{
public:

	StorageMass(uint32_t max_elements)
	{
		sg_buffer_desc buf_desc {};
		buf_desc.size                 = max_elements * sizeof(T);
		buf_desc.usage.storage_buffer = true;
		buf_desc.usage.dynamic_update = true;
		buf_desc.label                = "storage_mass";
		
		m_buffer = sg_make_buffer(&buf_desc);

		sg_view_desc view_desc {};
		view_desc.storage_buffer.buffer = m_buffer;
		
		m_view = sg_make_view(&view_desc);
	}

	~StorageMass()
	{
		if (m_view.id) {
			sg_destroy_view(m_view);
			m_view = {};
		}
		if (m_buffer.id) {
			sg_destroy_buffer(m_buffer);
			m_buffer = {};
		}
	}

	const T& get_raw(uint32_t index) const
	{
		HPR_ASSERT(index < m_raw_buffer.size());

		return m_raw_buffer[index];
	}

	const T& get_staged(uint32_t index) const
	{
		HPR_ASSERT(index < m_stage_buffer.size());

		return m_stage_buffer[index];
	}

	uint32_t push_raw(const T& data)
	{
		uint32_t index = static_cast<uint32_t>(m_raw_buffer.size());

		m_raw_buffer.push_back(data);

		return index;
	}

	uint32_t push_staged(const T& data)
	{
		uint32_t index = static_cast<uint32_t>(m_stage_buffer.size());

		m_stage_buffer.push_back(data);
		m_dirty = true;

		return index;
	}

	uint32_t stage(const mtp::vault<T, mtp::default_set>& mass)
	{
		if (mass.empty()) {
			return static_cast<uint32_t>(m_stage_buffer.size());
		}

		uint32_t base_idx = static_cast<uint32_t>(m_stage_buffer.size());
		uint32_t count    = static_cast<uint32_t>(mass.size());

		m_stage_buffer.resize(base_idx + count);

		std::memcpy(
			m_stage_buffer.data() + base_idx,
			mass.data(),
			count * sizeof(T)
		);

		m_dirty = true;
		
		return base_idx;
	}

	void sync()
	{
		if (!m_dirty || m_stage_buffer.empty()) {
			return;
		}

		sg_range range = {m_stage_buffer.data(), m_stage_buffer.size() * sizeof(T)};
		sg_update_buffer(m_buffer, &range);

		m_dirty = false;
	}

	void clear()
	{
		m_raw_buffer.clear();
		m_stage_buffer.clear();
		m_dirty = false;
	}

	StorageBind bind_state() const
	{
		return StorageBind {m_view};
	}

private:

	mtp::vault<T, mtp::default_set> m_raw_buffer;
	mtp::vault<T, mtp::default_set> m_stage_buffer;
	
	sg_buffer m_buffer {};
	sg_view m_view     {};
	bool m_dirty       {false};
};

} // hpr::rdr
