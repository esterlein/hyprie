#pragma once

#include "hprint.hpp"


namespace hpr::log {


struct PassStats
{
	uint32_t draw_calls {0};
	uint64_t indices    {0};
	uint64_t triangles  {0};
	uint32_t submeshes  {0};
	uint32_t materials  {0};

	PassStats& operator+=(const PassStats& right)
	{
		draw_calls += right.draw_calls;
		indices    += right.indices;
		triangles  += right.triangles;
		submeshes  += right.submeshes;
		materials  += right.materials;
		return *this;
	}

	friend PassStats operator+(PassStats left, const PassStats& right)
	{
		left += right;
		return left;
	}

	void reset()
	{
		*this = PassStats {};
	}
};


struct FrameStats
{
	uint64_t frame_index  {0};
	double   rdr_cpu_time {0.0};

	PassStats scene;
	PassStats total;

	void reset()
	{
		*this = FrameStats {};
	}
};


struct SceneLayerStats
{
	uint32_t frust_tested {0};
	uint32_t frust_culled {0};
	uint32_t occl_tested  {0};
	uint32_t occl_culled  {0};

	double hiz_raster_ms {0.0};
	double cull_job_ms   {0.0};
	double tlas_ms       {0.0};
	double raycast_ms    {0.0};

	void reset()
	{
		frust_tested = 0;
		frust_culled = 0;
		occl_tested  = 0;
		occl_culled  = 0;

		hiz_raster_ms = 0;
		cull_job_ms   = 0;
		tlas_ms       = 0;
	}
};


class StatsHarvester
{
public:

	StatsHarvester() = default;

	void begin_frame()
	{
		m_prev = m_curr;

		m_curr.frame.reset();
		m_curr.scene_layer.reset();

		m_curr.frame.frame_index = m_prev.frame.frame_index + 1;
	}

	FrameStats& frame_curr()
	{
		return m_curr.frame;
	}

	const FrameStats& frame_prev() const
	{
		return m_prev.frame;
	}

	SceneLayerStats& scene_layer_curr()
	{
		return m_curr.scene_layer;
	}

	const SceneLayerStats& scene_layer_prev() const
	{
		return m_prev.scene_layer;
	}

private:

	struct Payload
	{
		FrameStats      frame       {};
		SceneLayerStats scene_layer {};
	};

	Payload m_curr {};
	Payload m_prev {};
};


} // hpr::log
