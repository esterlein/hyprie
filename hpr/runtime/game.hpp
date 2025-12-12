#pragma once

#include "engine.hpp"

namespace hpr {

class Game
{
public:

	Game() = default;

	void init()
	{
		m_engine.init();
	}

	void tick()
	{
		m_engine.tick();
	}

	void update()
	{
		m_engine.update();
	}

	void frame(float delta)
	{
		m_engine.frame(delta);
	}

	void shutdown()
	{
		m_engine.shutdown();
	}

	Engine& engine()
	{
		return m_engine;
	}

private:

	Engine m_engine;
};

} // hpr
