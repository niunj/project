#!/usr/bin/env bash
# Run on the Ubuntu 22.04 SDK build machine when refreshing the binary layer.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <GDAL/PROJ/GEOS install> <oneAPI TBB Linux install>" >&2
    exit 2
fi
geo="$(realpath "$1")"
tbb="$(realpath "$2")"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
lib=lib/x86_64-linux-gnu
mkdir -p "$stage"/{include,lib/cmake,share/licenses,oneTBB/lib/intel64/gcc4.8,"$lib/cmake"}
# Preserve all geospatial headers. The SDK also contains the GEOS and PROJ interfaces.
cp -a "$geo"/include/. "$stage/include/"
cp -a "$geo"/lib/libgdal.so* "$geo"/lib/libgeos*.so* "$geo"/lib/libzip.so* "$geo"/lib/libproj.a "$geo"/lib/libsqlite3.a "$stage/lib/"
cp -a "$geo"/lib/cmake/{gdal,proj,proj4,GEOS,libzip} "$stage/lib/cmake/"
cp -a "$geo"/lib/gdalplugins "$stage/lib/"
cp -a "$geo"/share/{gdal,proj} "$stage/share/"
# PROJ's export references a static SQLite archive in the original prefix.
for file in "$stage"/lib/cmake/{proj,proj4}/*targets.cmake; do
    GEO_ORIGIN="$geo" python3 - "$file" <<'PY'
import os
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
path.write_text(text.replace(os.environ["GEO_ORIGIN"] + "/lib/libsqlite3.a", "${_IMPORT_PREFIX}/lib/libsqlite3.a"))
PY
done
cp -a /usr/include/OpenEXR /usr/include/zlib.h /usr/include/zconf.h "$stage/include/"
cp -a "/usr/$lib/cmake/"{OpenEXR,IlmBase} "$stage/$lib/cmake/"
for stem in IlmImf Imath Iex IlmThread Half z expat lzma deflate jpeg; do
    # Preserve runtime SONAME links and point absolute links back into the bundle.
    cp -a "/usr/$lib/lib$stem"*.so* "$stage/$lib/"
done
# VSG and Qt's CMake targets need the Linux client development interfaces.
# Use the same Ubuntu package headers and libraries at build and run time.
platform_packages=(libxcb1-dev libx11-dev libxau-dev libxdmcp-dev x11proto-dev
                   libgl-dev libglx-dev libopengl-dev libegl-dev libglvnd-dev)
for package in "${platform_packages[@]}"; do
    while read -r path; do
        case "$path" in
            /usr/include/*|"/usr/$lib/pkgconfig/"*.pc)
                if [[ -f "$path" ]]; then
                    (cd /usr && cp -a --parents "${path#/usr/}" "$stage/")
                fi
                ;;
        esac
    done < <(dpkg-query -L "$package")
    cp "/usr/share/doc/$package/copyright" "$stage/share/licenses/$package.txt"
done
for stem in xcb X11 Xau Xdmcp GL GLX GLdispatch OpenGL EGL; do
    cp -a "/usr/$lib/lib$stem.so"* "$stage/$lib/"
done
sed -i 's|^prefix=/usr$|prefix=${pcfiledir}/../../..|' "$stage/$lib/pkgconfig/"*.pc
find "$stage/$lib" -type l | while read -r link; do
    target="$(readlink "$link")"
    if [[ "$target" == /* ]]; then ln -sfn "${target##*/}" "$link"; fi
done
# These libraries may be dlopened or loaded indirectly by an executable
# with DT_RUNPATH. Let each library resolve the dependencies in its own bundle.
find "$stage/lib" -maxdepth 1 -type f -name '*.so.*' -print0 |
    xargs -0 -r -I '{}' patchelf --set-rpath '$ORIGIN:$ORIGIN/x86_64-linux-gnu' '{}'
find "$stage/$lib" -maxdepth 1 -type f -name '*.so.*' -print0 |
    xargs -0 -r -I '{}' patchelf --set-rpath '$ORIGIN' '{}'
cp -a "$tbb"/include "$tbb"/LICENSE.txt "$stage/oneTBB/"
cp -a "$tbb"/lib/cmake "$stage/oneTBB/lib/"
cp -a "$tbb"/lib/intel64/gcc4.8/libtbb.so* "$stage/oneTBB/lib/intel64/gcc4.8/"
for package in libopenexr-dev libilmbase-dev zlib1g libexpat1 liblzma5 libdeflate0 libjpeg-turbo8 libzip-dev libgdal-dev libproj-dev libgeos-dev libsqlite3-dev; do
    if [[ -f "/usr/share/doc/$package/copyright" ]]; then
        cp "/usr/share/doc/$package/copyright" "$stage/share/licenses/$package.txt"
    fi
done
find "$stage" -xtype l -print -quit | grep -q . && { find "$stage" -xtype l; exit 1; }
output="$repo/deps/linux-x86_64-ubuntu22.04.tar.gz"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -C "$stage" -cf - . | gzip -n > "$output.tmp"
mv "$output.tmp" "$output"
sha256sum "$output" | cut -d ' ' -f1 > "$output.sha256"
du -h "$output"
