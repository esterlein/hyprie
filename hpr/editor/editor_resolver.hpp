#include "handle_resolver.hpp"
#include "render_data.hpp"


namespace hpr::edt {


using EditorResolver = res::HandleResolver <
	res::ResolverEntry<rdr::MaterialInstance, const res::HandleStore<rdr::MaterialInstance>>,
	res::ResolverEntry<rdr::MaterialTemplate, const res::HandleStore<rdr::MaterialTemplate>>
>;

} // hpr::ui

