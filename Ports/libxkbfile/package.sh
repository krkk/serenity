#!/usr/bin/env -S bash ../.port_include.sh
port='libxkbfile'
version='1.1.3'
files=(
    "https://x.org/releases/individual/lib/libxkbfile-${version}.tar.xz#a9b63eea997abb9ee6a8b4fbb515831c841f471af845a09de443b28003874bec"
)
useconfigure='true'
configopts=(
    "--cross-file=${SERENITY_BUILD_DIR}/meson-cross-file.txt"
)
depends=(
    libX11
    xorgproto
)

export PKG_CONFIG_PATH="${SERENITY_INSTALL_ROOT}/usr/local/share/pkgconfig"

configure() {
    run meson _build "${configopts[@]}"
}

build() {
    run ninja -C _build
}

install() {
    export DESTDIR="${SERENITY_INSTALL_ROOT}"
    run meson install -C _build
}

