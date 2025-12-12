#include "handle_resolver.hpp"
#include "render_data.hpp"


namespace hpr::ui {


using UiResolver = res::HandleResolver <
	res::ResolverEntry<rdr::FontTexture, const res::HandleStore<rdr::FontTexture>>
>;

} // hpr::ui

