#!/bin/sh

set -e

readonly openvdb_version="11.0.0"
readonly openvdb_tarball="openvdb-$openvdb_version.tar.gz"
readonly openvdb_sha256sum="6314ff1db057ea90050763e7b7d7ed86d8224fcd42a82cdbb9c515e001b96c74"

readonly openvdb_root="$HOME/openvdb"

readonly openvdb_src="$openvdb_root/src"
readonly openvdb_build="$openvdb_root/build"

mkdir -p "$openvdb_root" \
    "$openvdb_src" "$openvdb_build"
cd "$openvdb_root"

echo "$openvdb_sha256sum  $openvdb_tarball" > openvdb.sha256sum
curl -OL "https://www.paraview.org/files/dependencies/openvdb-11.0.0.tar.gz"
sha256sum --check openvdb.sha256sum

tar -C "$openvdb_src" --strip-components=1 -xf "$openvdb_tarball"

cd "$openvdb_build"

cmake -GNinja "$openvdb_src/" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_LIBDIR=lib64 \
    -DUSE_BLOSC:BOOL=ON \
    -DUSE_ZLIB:BOOL=ON \
    -DUSE_CCACHE:BOOL=OFF \
    -DOPENVDB_BUILD_NANOVDB:BOOL=ON \
    -DUSE_NANOVDB:BOOL=ON \
    -DOPENVDB_CORE_STATIC:BOOL=OFF \
    -DTbb_INCLUDE_DIR:PATH=/usr/include \
    -DTbb_tbb_LIBRARY_RELEASE:PATH=/usr/lib64/libtbb.so \
    -DTbb_tbbmalloc_LIBRARY_RELEASE:PATH=/usr/lib64/libtbbmalloc.so
ninja
cmake --install .

cd
rm -rf "$openvdb_root"
