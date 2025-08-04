#!/usr/bin/env -S bash ../.port_include.sh
port='libXfont2'
version='2.0.7'
files=(
    "https://x.org/releases/individual/lib/libXfont2-${version}.tar.xz#8b7b82fdeba48769b69433e8e3fbb984a5f6bf368b0d5f47abeec49de3e58efb"
)
useconfigure='true'
depends=(
    freetype
    libfontenc
    xorgproto
    xtrans
    zlib
)

export PKG_CONFIG_PATH="${SERENITY_INSTALL_ROOT}/usr/local/share/pkgconfig"
