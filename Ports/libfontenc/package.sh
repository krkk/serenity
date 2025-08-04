#!/usr/bin/env -S bash ../.port_include.sh
port='libfontenc'
version='1.1.8'
files=(
    "https://x.org/releases/individual/lib/libfontenc-${version}.tar.xz#7b02c3d405236e0d86806b1de9d6868fe60c313628b38350b032914aa4fd14c6"
)
useconfigure='true'
depends=(
    xorgproto
    zlib
)

export PKG_CONFIG_PATH="${SERENITY_INSTALL_ROOT}/usr/local/share/pkgconfig"
