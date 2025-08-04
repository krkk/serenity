#!/usr/bin/env -S bash ../.port_include.sh
port='libxcvt'
version='0.1.3'
files=(
    "https://x.org/releases/individual/lib/libxcvt-${version}.tar.xz#a929998a8767de7dfa36d6da4751cdbeef34ed630714f2f4a767b351f2442e01"
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

