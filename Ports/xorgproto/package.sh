#!/usr/bin/env -S bash ../.port_include.sh
port='xorgproto'
version='2024.1'
files=(
    "https://www.x.org/releases/individual/proto/xorgproto-${version}.tar.xz#372225fd40815b8423547f5d890c5debc72e88b91088fbfb13158c20495ccb59"
)
useconfigure='true'
configopts=(
    "--cross-file=${SERENITY_BUILD_DIR}/meson-cross-file.txt"
)

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
