# hyprie

`hyprie` is a custom 3D engine built for a simulation-driven tile world.

<p align="center">
  <img src="./test_scene/test_cut_00.webp" width="100%">
</p>


### :white_square_button: BVH and raycasting

Simple and relatively fast mouse picking solutions require reading memory from the GPU. I decided to avoid that and instead unify CPU-side raycasting as an engine-wide feature and build a production-grade fast acceleration structure instead. As a result, the mouse picking system gained characteristics of a ray tracer, but the final system is easily extendable to handle physics events as well (there won't be a complete ragdoll physics subsystem, but some occasional/clustered physics event handling is unavoidable). That's why the presented overengineered mouse picker is just a demonstration of a more fundamental engine feature.

The TLAS is rebuilt per frame and handles ECS-synchronized world state, while the BLAS is baked into the SoA scene rigs at the asset import stage. BLAS roots are built per-submesh (gltf primitive) and each BLAS natively handles per-frame transforms. The TLAS is built as a wide HLBVH utilizing Morton code treelet branching and 16-bin SAH at the top level only. The BLAS is a standard high-quality 16-bin SAH wide BVH, quantized to 8-bit for optimizing memory bandwidth and cache locality.

The papers I based the final two-tier BVH on:

- *Fast BVH Construction on GPUs* (Lauterbach et al.)
- *HLBVH: Hierarchical Bounding Volume Hierarchies* (Garanzha et al.)
- *Wide BVH Traversal with a Short Stack* (Ylitie et al.)

The following video demonstration presents per-frame TLAS reconstruction and a BLAS ray traversal visualization. Blue-green color is TLAS, green color is BLAS (roots only). For ray traversal overlays: purple color shows tested nodes, red color shows the actual ray path.


- [test_bvh_00](test_scene/test_bvh_00.mp4)
- [test_bvh_01](test_scene/test_bvh_01.mp4)


### :white_square_button: visibility system

`sokol` has proven to be a solid choice as a clean abstraction layer to build the core systems around. It uses Pipeline State Objects, so the rest of the engine naturally aligns with a modern API.
The downside is that it doesn't support indirect draws, which rules out a few techniques, such as GPU-driven occlusion culling without readbacks.

A CPU Hi-Z prepass proved to be a viable alternative. After experimenting with several approaches that resulted in submesh flickering, I settled on generating convex hulls for every non-skinned submesh (everything is an occludee) and authoring low-poly twins in Blender for meshes intended to act as occluders.

The occluder twin is strictly enclused by the visible model. An AABB frustum culling test before the occlusion test is necessary for performance.

The demonstration below is running on an AMD Ryzen 7 Linux laptop with integrated Radeon 780M graphics, rendering relatively complex models (around 20 million triangles total, which is a bottleneck on this hardware).

https://github.com/user-attachments/assets/e41e9fd6-a920-4b3e-8d18-c7a2c3d9dca0

The resulting main scene submission pipeline looks like this (only the main culling stage is currently parallelized):

<p align="center">
  <img src="./test_scene/scene_submission_00.png" width="100%">
</p>


### :white_square_button: engine features

**Core**
- Compile-time configurable [pool allocator](https://github.com/esterlein/metapool)
- Chase-Lev work-stealing scheduler with DV-MPMC injection
- Custom ECS with hierarchical transform graph
- CPU-side 8-ary HLBVH TLAS (Morton encoding with radix sort + top-level 16-bin SAH)
- CPU-side 8-ary Quantized BVH BLAS (8-bit nodes, per-axis 16-bin SAH)
- BVH-accelerated SIMD raycast (non-recursive TLAS/BLAS stacks traversal)
- Engine-native QuickHull DOD implementation
- Asset system with resource deduplication
- TOML-based scene parser

**Rendering**
- Command-based multi-pass renderer
  - Instancing
  - Batching
- Data-oriented submission
  - Texture array indexing
  - Unified geometry buffers
- Material system with fixed ORM(H) + emissive PBR
- Cook–Torrance GGX microfacet BRDF (Smith masking-shadowing, Schlick Fresnel)
- Multi-light forward shading (directional, point, spot)
- Image-based lighting
- Mask-based object outline
- sokol-gfx backend

**Visibility**
- Multithreaded SIMD frustum culling
- Multithreaded SIMD Hi-Z occlusion culling

**Simulation**
- Layered sparse tilemap

**Tools**
- glTF / GLB loader
- Gizmo system (wip)


### :white_square_button: roadmap

- Skeletal animation
- LOD system
- Render DAG
  - Shadow mapping
  - Tone mapping
  - Volumetric fog
- Clustered lighting


<p align="center">
  <img src="./test_scene/dragons_1.webp" width="100%">
</p>
<p align="center">
  <img src="./test_scene/dragons_2.webp" width="100%">
</p>


### :white_square_button: build & run

Requires cmake, clang++ or g++, and [sokol-shdc](https://github.com/floooh/sokol-tools-bin) in `$PATH` or via `SHDC=`.

```bash
./setup_submodules.sh
./build.sh shd          # only generate shaders
./build.sh              # build
./build.sh run          # build and run
./build.sh clean        # clean and build
./build.sh clean run    # clean, build and run
```

