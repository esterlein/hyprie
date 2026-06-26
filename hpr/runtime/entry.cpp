#include <chrono>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"

#include "mtp_memory.hpp"

#include "log.hpp"
#include "game.hpp"
#include "renderer.hpp"


struct GlobalContext
{
	hpr::Game game;
};

static std::unique_ptr<GlobalContext> global_ctx;


void init()
{
	mtp::init_tls<mtp::default_set>();
	hpr::log::set_level(hpr::log::LogLevel::debug);

	sg_desc gfx_desc {};
	gfx_desc.environment = sglue_environment();
	gfx_desc.logger.func = slog_func;

	gfx_desc.buffer_pool_size   = 1024;
	gfx_desc.image_pool_size    = 512;
	gfx_desc.pipeline_pool_size = 512;

	sg_setup(&gfx_desc);

	global_ctx = std::make_unique<GlobalContext>();
	global_ctx->game.init();
}

void frame()
{
	static auto last = std::chrono::steady_clock::now();
	auto now         = std::chrono::steady_clock::now();
	float delta      = std::chrono::duration<float>(now - last).count();

	last = now;

	global_ctx->game.tick();
	global_ctx->game.update();
	global_ctx->game.frame(delta);
}

void cleanup()
{
	global_ctx->game.shutdown();
	global_ctx.reset();

	sg_shutdown();

	mtp::get_tls_allocator<mtp::default_set>().reset();
}

void event(const sapp_event* event)
{
	global_ctx->game.engine().on_event(event);
}

sapp_desc sokol_main(int, char**)
{
	sapp_desc desc {};
	desc.init_cb          = init;
	desc.frame_cb         = frame;
	desc.cleanup_cb       = cleanup;
	desc.event_cb         = event;
	desc.width            = 1920;
	desc.height           = 1200;
	desc.window_title     = "hyprie";
	desc.logger.func      = slog_func;
	desc.gl.major_version = 4;
	desc.gl.minor_version = 6;
	return desc;
}
