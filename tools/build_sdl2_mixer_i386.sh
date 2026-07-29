#!/usr/bin/env bash

set -euo pipefail

readonly VERSION="2.8.1"
readonly SHA256="cb760211b056bfe44f4a1e180cc7cb201137e4d1572f2002cc1be728efd22660"
readonly URL="https://github.com/libsdl-org/SDL_mixer/releases/download/release-${VERSION}/SDL2_mixer-${VERSION}.tar.gz"
readonly PREFIX="${1:?usage: $0 PREFIX}"
readonly BUILD_ROOT="${2:?usage: $0 PREFIX BUILD_ROOT}"
readonly PKG_CONFIG_LIBDIR_VALUE="/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig"
readonly ARCHIVE="${BUILD_ROOT}/SDL2_mixer-${VERSION}.tar.gz"
readonly SOURCE_DIR="${BUILD_ROOT}/SDL2_mixer-${VERSION}"
readonly STAMP="${PREFIX}/.version"

if [[ -f "${PREFIX}/lib/libSDL2_mixer.a" && -f "${PREFIX}/include/SDL2/SDL_mixer.h" && -f "${STAMP}" ]] \
    && [[ "$(<"${STAMP}")" == "${VERSION}" ]]; then
    exit 0
fi

mkdir -p "${BUILD_ROOT}"

if [[ ! -f "${ARCHIVE}" ]] || ! printf '%s  %s\n' "${SHA256}" "${ARCHIVE}" | sha256sum -c - >/dev/null 2>&1; then
    rm -f "${ARCHIVE}"
    curl --fail --location "${URL}" --output "${ARCHIVE}"
fi
printf '%s  %s\n' "${SHA256}" "${ARCHIVE}" | sha256sum -c -

rm -rf "${SOURCE_DIR}" "${PREFIX}"
tar -xzf "${ARCHIVE}" -C "${BUILD_ROOT}"

export PKG_CONFIG_LIBDIR="${PKG_CONFIG_LIBDIR_VALUE}"
export CC=gcc
export CFLAGS="-m32 -O2"
export LDFLAGS="-m32"
export SDL_CFLAGS="$(pkg-config --cflags sdl2)"
export SDL_LIBS="$(pkg-config --libs sdl2)"

pushd "${SOURCE_DIR}" >/dev/null
./configure \
    --prefix="${PREFIX}" \
    --disable-shared \
    --enable-static \
    --disable-music-cmd \
    --disable-music-mod \
    --disable-music-midi \
    --disable-music-gme \
    --enable-music-ogg \
    --enable-music-ogg-stb \
    --disable-music-ogg-vorbis \
    --enable-music-mp3 \
    --enable-music-mp3-minimp3 \
    --disable-music-mp3-mpg123 \
    --disable-music-flac \
    --disable-music-opus \
    --disable-music-wavpack

make -j"${JOBS:-4}"
make install
popd >/dev/null
printf '%s\n' "${VERSION}" > "${STAMP}"
