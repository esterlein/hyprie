#pragma once

#include <cstdint>

#include "nuklear_cfg.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "render_data.hpp"
#include "tile_draw_data.hpp"


namespace hpr::rdr {


namespace cfg {

static constexpr size_t max_debug_text_lines = 256U;

} // hpr::rdr::cfg

enum class SceneDrawCmdFlag : uint8_t
{
	None     = 0,
	Selected = 1 << 0
};


struct SceneDrawCommand
{
	Handle<Mesh>             mesh;
	uint32_t                 submesh_idx;
	Handle<MaterialInstance> material;
	uint64_t                 sort_key;
	uint32_t                 layer_index;
	mat4                     mtx_M;
	uint8_t                  flags;
};


struct OverlayDrawCommand
{
	Handle<Mesh>    mesh;
	uint32_t        submesh_idx;
	uint32_t        sort_key;
	uint32_t        layer_index;
	mat4            mtx_m;
	vec4            rgba;
};


struct FxDrawCommand
{
	uint32_t sort_key;
	uint32_t layer_index;
	uint8_t  kind;
	uint8_t  params_size;

	std::array<uint8_t, 32> params;
};


struct TileDrawCommand
{
	Handle<Mesh>      mesh;
	uint32_t          submesh_idx;
	Handle<Texture>   tilemap;
	Handle<TileStyle> tile_style;
	uint64_t          sort_key;
	uint32_t          layer_index;
	mat4              mtx_M;
};


// TODO: cache ui font state when ownership is clean

struct UiDrawCommand
{
	nk_context*          ctx;
	uint32_t             layer_index;
	Handle<FontTexture>  font_texture;
	nk_draw_null_texture null_texture;
};


struct DebugTextLine
{
	uint32_t    color;
	vec2        position;
	const char* text;
};


struct DebugDrawCommand
{
	uint32_t            layer_index;
	uint32_t            line_count;
	Handle<FontTexture> font_texture;
	DebugTextLine       lines[cfg::max_debug_text_lines];
};

} // hpr::rdr

