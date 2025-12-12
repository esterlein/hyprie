#include "handle_resolver.hpp"
#include "render_data.hpp"


namespace hpr::scn {


using SceneResolver = res::HandleResolver <
	res::ResolverEntry<rdr::Mesh,            const res::HandleStore<rdr::Mesh>>,
	res::ResolverEntry<rdr::MeshGeometry,    const res::HandleStore<rdr::MeshGeometry>>,
	res::ResolverEntry<rdr::MaterialInstance,      res::HandleStore<rdr::MaterialInstance>>

>;

} // hpr::scn

