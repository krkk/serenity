#!/usr/bin/env -S bash ../.port_include.sh
port='libX11'
version='1.8.12'
files=(
    "https://www.x.org/releases/individual/lib/libX11-${version}.tar.xz#fa026f9bb0124f4d6c808f9aef4057aad65e7b35d8ff43951cef0abe06bb9a9a"
)
useconfigure='true'
depends=(
    libxcb
    xorgproto
    xtrans
)
configopts=(
    '--disable-xthreads' # FIXME: Support multithreading
)

export PKG_CONFIG_PATH="${SERENITY_INSTALL_ROOT}/usr/local/share/pkgconfig"
