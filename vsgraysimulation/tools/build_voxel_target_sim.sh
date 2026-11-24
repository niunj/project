#!/usr/bin/env bash
# Build tools/voxel_target_sim.cpp against the existing build tree.
# Reuses the exact compile/link parameters CMake generated for
# sim_earth_voxel_model_tests so the tool links the same libraries.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD=build
OUT="$BUILD/voxel_target_sim"

DEFINES=(
  -DROCKY_STATIC -DVSGBULLET_STATIC
  -DSIM_SOURCE_ROOT="\"$ROOT\""
  -DVSGRAYSIM_GDAL_DATA_PATH='"/home/mte/Code/earth_3rdparty/share/gdal"'
  -DVSGRAYSIM_PROJ_DATA_PATH='"/home/mte/Code/earth_3rdparty/share/proj"'
  -DVSGRAYSIM_ROCKY_FILE_PATH="\"$BUILD/share/rocky\""
  -DVSGRAYSIM_WITH_ROCKY=1
)

INCLUDES=(
  -I"$ROOT/env_modules/src" -I"$ROOT/src" -I"$ROOT/src/earth" -I"$ROOT/include"
  -I"$ROOT/env_modules/include" -I"$ROOT/third_party/vsg/include"
  -I"$BUILD/third_party/vsg/include" -I"$ROOT/lib/simcore/include"
  -I"$ROOT/third_party/rocky/src" -I"$BUILD/third_party/rocky/build_include"
  -I"$ROOT/third_party/json/include" -I"/home/mte/Code/earth_3rdparty/include"
  -I"/home/mte/Code/bullet3/install/include/bullet"
  -I"$ROOT/third_party/vsgXchange/include" -I"$BUILD/third_party/vsgXchange/include"
  -I"$ROOT/third_party/vsgImGui/include" -I"$BUILD/third_party/vsgImGui/include/vsgImGui"
  -I"$ROOT/third_party/vsgImGui/include" -I"$BUILD/third_party/vsgImGui/include/vsgImGui"
  -I"$ROOT/third_party/vsgBullet/include" -I"$ROOT/lib/ipc"
  -isystem /home/mte/Code/vulkan/1.3.296.0/x86_64/include
  -isystem "$ROOT/third_party/glm" -isystem "$ROOT/third_party/spdlog/include"
  -isystem "$ROOT/third_party/entt/src" -isystem /home/mte/Code/Cuda/cuda-12.4/include
)

# Source files: just the tool. The voxel-earth sources it drives
# (VoxelTerrain/VoxelTerrainPhysics/VoxelModelVoxelizer/VoxelTerrainDebris)
# are already compiled into libvsgraysim_voxel_earth.a and libvsgraysim_core.a.
SOURCES=(
  tools/voxel_target_sim.cpp
)

# Libraries: same closure CMake used for the voxel model tests.
LIBS=(
  "$BUILD/env_modules/libvsgraysim_rocky_env.a"
  "$BUILD/third_party/rocky/src/rocky/librocky.a"
  "$BUILD/lib/voxel_earth/libvsgraysim_voxel_earth.a"
  "$BUILD/libvsgraysim_core.a"
  "$BUILD/env_modules/libvsgraysim_rocky_env.a"
  "$BUILD/lib/voxel_earth/libvsgraysim_voxel_earth.a"
  "$BUILD/lib/voxel_earth/libvsgraysim_earth_globe.a"
  "$BUILD/lib/voxel_earth/libvsgraysim_earth_environment.a"
  "$BUILD/third_party/rocky/src/rocky/librocky.a"
  /home/mte/Code/earth_3rdparty/lib/libproj.a -lm -ldl
  /home/mte/Code/earth_3rdparty/lib/libsqlite3.a
  /home/mte/Code/earth_3rdparty/lib/libgdal.so
  "$BUILD/lib/voxel_earth/libvsgraysim_earth_shaders.a"
  "$BUILD/lib/objloader/libvsgraysim_objloader.a"
  /home/mte/Code/earth_3rdparty/lib/libgdal.so
  "$BUILD/libvsgraysim_cuda.so"
  "$BUILD/lib/ipc/libvsgraysim_ipc.a"
  "$BUILD/third_party/vsgXchange/lib/libvsgXchange.a"
  /usr/lib/x86_64-linux-gnu/libIlmImf-2_5.so.25.0.6
  /usr/lib/x86_64-linux-gnu/libz.so
  /usr/lib/x86_64-linux-gnu/libImath-2_5.so.25.0.6
  /usr/lib/x86_64-linux-gnu/libHalf-2_5.so.25.0.6
  /usr/lib/x86_64-linux-gnu/libIexMath-2_5.so.25.0.6
  /usr/lib/x86_64-linux-gnu/libIlmThread-2_5.so.25.0.6
  /usr/lib/x86_64-linux-gnu/libIex-2_5.so.25.0.6
  /home/mte/Code/Cuda/cuda-12.4/lib64/libcudart.so
  /usr/lib/x86_64-linux-gnu/libcuda.so
  "$BUILD/third_party/vsgImGui/lib/libvsgImGui.a"
  "$BUILD/third_party/vsgBullet/lib/libvsgbDynamics.a"
  "$BUILD/third_party/vsgBullet/lib/libvsgbCollision.a"
  "$BUILD/third_party/vsg/lib/libvsg.a"
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libglslang-default-resource-limits.a
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libSPIRV.a
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libglslang.a
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libSPIRV-Tools-opt.a
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libSPIRV-Tools.a
  -lrt
  /home/mte/Code/vulkan/1.3.296.0/x86_64/lib/libvulkan.so
  /usr/lib/x86_64-linux-gnu/libxcb.so
  /home/mte/Code/bullet3/install/lib/libBulletDynamics.so
  /home/mte/Code/bullet3/install/lib/libBulletCollision.so
  /home/mte/Code/bullet3/install/lib/libLinearMath.so
  /home/mte/Code/bullet3/install/lib/libBulletSoftBody.so
  /home/mte/Code/bullet3/install/lib/libBulletWorldImporter.so
  /home/mte/Code/bullet3/install/lib/libConvexDecomposition.so
  /home/mte/Code/bullet3/install/lib/libBulletFileLoader.so
  /home/mte/Code/bullet3/install/lib/libBullet2FileLoader.so
  -lzip
)

RPATH="-Wl,-rpath,$BUILD:/home/mte/Code/vulkan/1.3.296.0/x86_64/lib:$BUILD/third_party/vsg/lib:$BUILD/third_party/vsgXchange/lib:$BUILD/third_party/vsgImGui/lib:$BUILD/third_party/vsgBullet/lib:$BUILD/third_party/rocky/src/rocky:/home/mte/Code/bullet3/install/lib:/home/mte/Code/earth_3rdparty/lib:/home/mte/Code/Cuda/cuda-12.4/lib64"

/usr/bin/c++ -O3 -DNDEBUG -std=gnu++17 -fPIE \
  "${DEFINES[@]}" "${INCLUDES[@]}" "${SOURCES[@]}" -o "$OUT" \
  "${LIBS[@]}" "$RPATH" -Wl,--disable-new-dtags

echo "Built $OUT"
