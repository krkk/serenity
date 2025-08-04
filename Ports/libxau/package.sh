#!/usr/bin/env -S bash ../.port_include.sh
port='libxau'
version='1.0.12'
files=(
    "https://www.x.org/releases/individual/lib/libXau-${version}.tar.xz#74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb"
)
useconfigure='true'
configopts=(
    "--cross-file=${SERENITY_BUILD_DIR}/meson-cross-file.txt"
)
workdir="libXau-${version}"
depends=(
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
