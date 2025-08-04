#!/usr/bin/env -S bash ../.port_include.sh
port='libxcb'
version='1.17.0'
files=(
    "https://x.org/releases/individual/xcb/libxcb-${version}.tar.xz#599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55"
)
useconfigure='true'
depends=(
    libxau
    xcbproto
)

export PKG_CONFIG_PATH="${SERENITY_INSTALL_ROOT}/usr/local/share/pkgconfig"
