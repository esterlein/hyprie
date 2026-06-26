#include "handle_resolver.hpp"
#include "render_data.hpp"


namespace hpr::ui {


using UiResolver = res::HandleResolver <
	res::ResolverEntry<rdr::Texture, const res::HandleStore<rdr::Texture>>
>;

} // hpr::ui

