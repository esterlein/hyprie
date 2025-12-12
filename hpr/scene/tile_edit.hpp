#pragma once

#include "mtp_memory.hpp"

#include "tile_field.hpp"


namespace hpr::scn {


struct TileEdit
{
	TileCoord coord;
	TileType  before;
	TileType  after;
};


class TileEditBatch
{
public:

	void clear()
	{
		m_edits.clear();
	}

	void push(TileCoord coord, TileType before, TileType after)
	{
		m_edits.emplace_back(TileEdit {coord, before, after});
	}

	[[nodiscard]] bool empty() const
	{
		return m_edits.empty();
	}

	[[nodiscard]] const mtp::vault<TileEdit, mtp::default_set>& edits() const
	{
		return m_edits;
	}

	template <typename MarkDirtyFn>
	uint32_t apply(TileField& tilefield, MarkDirtyFn&& mark_dirty)
	{
		uint32_t applied = 0;

		for (const TileEdit& edit : m_edits) {

			TileType* tile_ptr = tilefield.get_ptr(edit.coord);

			if (!tile_ptr)
				continue;

			const TileType current = *tile_ptr;

			if (current != edit.before)
				continue;

			if (current == edit.after)
				continue;

			*tile_ptr = edit.after;
			mark_dirty(edit.coord);
			++applied;
		}

		return applied;
	}

private:

	mtp::vault<TileEdit, mtp::default_set> m_edits;
};


} // hpr::scn

