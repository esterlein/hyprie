#pragma once

#include "handle.hpp"

#include "texture_data.hpp"
#include "storage_data.hpp"
#include "vertex_format.hpp"

#include "sokol_gfx.h"

#include <array>


namespace hpr::rdr {


template <typename T>
class StorageMass;

template <typename Vertex, typename Index>
class VertexMass;


struct Pipeline
{
	sg_shader   shader   {};
	sg_pipeline pipeline {};
};


struct PipelineSet
{
	Pipeline scene_static;
	Pipeline scene_skinned;
	Pipeline cue;
	Pipeline cue_wire;
	Pipeline overlay;
	Pipeline overlay_wire;
	Pipeline grid;
	Pipeline outline_mask;
	Pipeline outline_dilate;
	Pipeline outline_blend;
	Pipeline bitmap;
	Pipeline skybox;
	Pipeline ibl_equirect;
	Pipeline ibl_irradiance;
	Pipeline ibl_prefilter;
	Pipeline ibl_brdf;
};


struct TextureBind
{
	std::array<sg_view, cfg::num_tex_arrays> views {};
	uint32_t count {0};
};


struct SamplerBind
{
	std::array<sg_sampler, static_cast<size_t>(SamplerType::count)> types;
};


struct AtlasBind
{
	sg_view view {};
};


struct NuklearAtlasBind
{
	sg_image   image   {};
	sg_view    view    {};
	sg_sampler sampler {};
};


struct VertexBind
{
	sg_buffer vtx_buf;
	sg_buffer idx_buf;
};


struct StorageBind
{
	sg_view view;
};


struct TargetFramebuffs
{
	sg_image mask_img;
	sg_view  mask_draw_view;
	sg_view  mask_smpl_view;

	sg_image dilate_img;
	sg_view  dilate_draw_view;
	sg_view  dilate_smpl_view;
};


struct EnvironmentBind
{
	sg_image equirect_src_img;
	sg_image env_cube_img;
	sg_image irr_cube_img;
	sg_image pref_cube_img;
	sg_image brdf_lut_img;

	sg_view env_view;
	sg_view irr_view;
	sg_view pref_view;
	sg_view brdf_view;
};


struct BindingContext
{
	TextureBind fonts;
	TextureBind texarrays;
	TextureBind tilemaps;
	AtlasBind   palettes;

	SamplerBind samplers;

	VertexBind scn_vtx;
	VertexBind anm_vtx;
	VertexBind gen_vtx;
	VertexBind btm_vtx;

	StorageBind scn_blob_ssbo;
	StorageBind anm_blob_ssbo;
	StorageBind anm_bones_ssbo;
	StorageBind cue_blob_ssbo;
	StorageBind orl_blob_ssbo;
	StorageBind mat_inst_ssbo;

	StorageBind vtx_gen_ssbo;
	StorageBind idx_gen_ssbo;

	EnvironmentBind environment;

	PipelineSet pipelines;

	TargetFramebuffs targets;

	NuklearAtlasBind nk_atlas;
};


struct StagingContext
{
	StorageMass<SceneBlob>*              scn_blob_mass {nullptr};
	StorageMass<CueBlob>*                cue_blob_mass {nullptr};
	StorageMass<OverlayBlob>*            orl_blob_mass {nullptr};
	StorageMass<AnimBlob>*               anm_blob_mass {nullptr};

	StorageMass<MaterialBlob>*           mat_blob_mass {nullptr};

	VertexMass<SceneVertex,   uint32_t>* scn_vtx_mass  {nullptr};
	VertexMass<GenericVertex, uint32_t>* gen_vtx_mass  {nullptr};
	VertexMass<BitmapVertex,  uint16_t>* btm_vtx_mass  {nullptr};
};

} // hpr::rdr
