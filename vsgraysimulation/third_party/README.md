# Managed graphics dependency sources

This directory contains the actual dependency sources used by the local-source
build. A checkout or source export includes these files; CMake does not clone a
branch or fetch missing submodules while configuring. Build products belong in
the build tree, outside these source directories.

## Pinned baseline

| Source directory | Version | Upstream revision |
| --- | --- | --- |
| `vsg` | 1.1.15 | `599a8c5c61cbb993079261b2ada99bd843badf5b` |
| `vsgXchange` | 1.1.14 | `d2a93c1eeda7b121537f0a077b192f675d1d045b` |
| `vsgImGui` | 0.7.0 | `37ef59ffa78482ee50b96c17b03bf3e0fbf0f973` |
| `vsgImGui/src/imgui` | 1.91.6 | `993fa347495860ed44b83574254ef2a317d0c14f` |
| `vsgImGui/src/implot` | 0.17 | `f156599faefe316f7dd20fe6c783bf87c8bb6fd9` |
| `vsgBullet` | 3.00.00 | `cdc7e3b2b0bcbc88567b46f3f9eb11ccefe1361b` |
| `rocky` | 1.1.0, post-tag ABI 52 | `558d1b6b116e1c53e9c6ac6a1c942c2acafd33be` |
| `glm` | 0.9.9.8 | `bf71a834948186f4097caa076cd2663c69a10e1e` |
| `json` | 3.11.2 | `bc889afb4c5bf1c0d8ee29ef35eaaf4c8bef8a5d` |
| `spdlog` | 1.10.0 | `76fb40d95455f249bd70824ecfcae7a8f0930fa3` |
| `entt` | 3.13.2 | `78213075654a688e9da6bc49f7f873d25c26d12c` |

`upstream.lock.json` records the repository, full commit, archive URL and real
SHA-256 digest for each import. `manifests/*.json` records every imported file
and its SHA-256, including nested sources. The archive hashes identify upstream
downloads; the manifests identify the expanded source trees and reviewed local
changes. They are deliberately different records.

The local VSG patch tracks the image and semaphore returned by a suboptimal
swapchain acquire and presents that image. This preserves forward progress when
a desktop surface reports suboptimal presentation during streamed rendering.

vsgXchange is pinned before the upstream glTF meshlet changes that require a newer
VSG ShaderSet API. Its public source headers match the previous installation.

Rocky's `v1.1.0` tag uses ABI 51, while this application's previous installation
used ABI 52. The pinned post-tag commit preserves the production MapNode,
VSGContext, ViewDependentState, DateTime, Ephemeris, GeoPoint and Map API headers.
It is not represented as an exact reconstruction of that unidentified installed
binary. The local GDAL adapter accepts the read-only metadata list returned by
GDAL 3.14. Application and the ECS label system honor Rocky's disabled built-in ImGui
option when including headers, compiling code and registering default systems. The lock records each change
separately from the upstream archive.
All consumers must rebuild against the same local-source baseline.

The preinstalled vsgImGui package reported version 0.7.0 but contained an
inconsistent ImGui 1.90.1 WIP / ImPlot 1.1 WIP header mixture. This tree uses the
exact nested revisions recorded by upstream v0.7.0. The public RenderImGui API
header matches the previous installation; application build and regression
tests validate the coherent source baseline.

All 160 imported vsgBullet files matched its recovered upstream commit
byte-for-byte. Its approximately 35 MB of sample model assets under `data/` are
omitted, except for `data/CMakeLists.txt`; production source, headers, examples,
build files and license are retained. The dependency provider disables sample
data installation and sample applications. Old local `build/` and `install/`
artifacts are not part of this source import.

## Making and reviewing changes

Run source verification from the repository root:

```bash
python3 tools/dependencies/verify_sources.py --repo .
```

Edit dependency sources directly when an engine change is needed. Review and
test the change, then explicitly update the reviewed manifests with a reason:

```bash
python3 tools/dependencies/verify_sources.py --repo . \
  --record-local-changes vsg --reason "Describe the reviewed engine change"
```

For nested ImGui/ImPlot edits, both the component and its parent vsgImGui tree
must be reviewed; the tool supports explicit multi-tree refresh. Do not change
an upstream commit or archive digest to conceal a local patch. An upstream
upgrade is a separate source import followed by compatibility tests and an
explicit lock update. The tool never silently blesses changed or missing files.

The parent project owns build integration in `cmake/`. Generated public headers
are placed in the build directory so configuring or compiling does not mutate
these locked upstream trees. Vulkan/CUDA SDKs, Qt, Bullet, GDAL/PROJ and other
platform libraries remain separately configured prerequisites in this phase;
see `docs/build_dependencies.md` for the supported build entry points and scope.

## Licenses

Upstream license files and source notices are retained. VSG, vsgXchange,
vsgImGui, ImGui, ImPlot, Rocky, JSON, spdlog and EnTT use MIT terms. VSG also
includes Vulkan-Headers notices under Apache-2.0. GLM's original dual-license
notice is retained in `glm/copying.txt`, including its MIT option. spdlog's
bundled fmt notices remain in its source headers. vsgBullet uses LGPL-2.1;
its license must not be relabeled as MIT. The exact notice paths are recorded
per dependency in the lock file.
