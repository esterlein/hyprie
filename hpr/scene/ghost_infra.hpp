#pragma once

#include <cstdint>

#include "mtp_memory.hpp"

#include "tile_data.hpp"


namespace hpr::scn {

// NOTE: placeholder ghost infra types, temporary location

enum class GhostChannel : uint8_t
{
	power = 0,
	data,
	pipe,
	count
};


enum class GhostPort : uint8_t
{
	center = 0,

	north,
	east,
	south,
	west,

	up,
	down
};


struct GhostNode
{
	TileCoord tile;
	GhostPort port;
	GhostChannel channel;
};


struct GhostEdge
{
	uint32_t a;
	uint32_t b;
	GhostChannel channel;

	// TODO: per-edge damage state - broken segments

	uint8_t flags {0};
};


struct GhostGraph
{
	mtp::vault<GhostNode, mtp::default_set> nodes;
	mtp::vault<GhostEdge, mtp::default_set> edges;
};


struct GhostInfra
{
	GhostGraph power;
	GhostGraph data;
	GhostGraph pipe;
};


} // hpr::scn

