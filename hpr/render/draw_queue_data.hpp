#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "nuklear_cfg.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "font_data.hpp"
#include "render_data.hpp"
#include "draw_tile_data.hpp"

#include <array>


namespace hpr::rdr {


namespace cfg {

inline constexpr uint8_t max_fx_payload_size = 64U;

} // hpr::rdr::cfg


enum class SceneDrawCmdFlag : uint8_t
{
	none     = 0,
	selected = 1 << 0
};


struct SceneDrawCmd
{
	uint64_t sort_key;
	uint32_t layer_idx;

	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;
	uint32_t trs_idx;

	uint32_t mat_idx;

	uint8_t  flags;
};


struct ReplayDrawCmd
{
	uint64_t sort_key;

	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;
	uint32_t trs_idx;
};


struct CueDrawCmd
{
	uint64_t sort_key;
	uint32_t layer_idx;

	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;
	uint32_t trs_idx;

	uint32_t cue_mask;
	uint32_t tilemap_slice;
	uint32_t palette_slice;
};


struct OverlayDrawCmd
{
	uint64_t sort_key;
	uint32_t layer_idx;

	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;
	uint32_t trs_idx;

	uint32_t flags;
};


struct FxDrawCmd
{
	uint32_t sort_key;
	uint32_t layer_idx;

	uint8_t  kind;
	uint8_t  payload_size;

	std::array<uint8_t, cfg::max_fx_payload_size> payload;
};



struct UiDrawCmd
{
	nk_context*          ctx;
	uint32_t             layer_index;
	Handle<Texture>      font_texture;
	nk_draw_null_texture null_texture;
};


struct DebugTextLine
{
	int32_t               cell_x;
	int32_t               cell_y;
	uint32_t              color;
	std::array<char, 128> text;
};


struct DebugDrawCmd
{
	uint64_t sort_key;
	uint32_t layer_idx;

	Handle<Font> font;

	mtp::vault<DebugTextLine, mtp::default_set> lines;

	DebugDrawCmd() = default;

	DebugDrawCmd(const DebugDrawCmd&) = delete;
	DebugDrawCmd& operator=(const DebugDrawCmd&) = delete;

	DebugDrawCmd(DebugDrawCmd&&) noexcept = default;
	DebugDrawCmd& operator=(DebugDrawCmd&&) noexcept = default;
};


} // hpr::rdr

