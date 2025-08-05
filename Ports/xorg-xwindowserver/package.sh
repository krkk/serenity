#!/usr/bin/env -S bash ../.port_include.sh
port='xorg-xwindowserver'
version='21.1.18'
files=(
    "git+https://gitlab.freedesktop.org/xorg/xserver.git#061690c2e649ce41ae277dd7555ad90855376e31"
)
useconfigure='true'
configopts=(
    '--cross-file'
    "${SERENITY_BUILD_DIR}/meson-cross-file.txt"
    '-Dipv6=false' # im so soryy.
    '-Dsha1=libsha1'
    '-Dxorg=false'
    '-Dxwayland=false'
    '-Dglamor=false'
	'-Dxvfb=false'
	'-Dglx=false'
	'-Dxdmcp=false'
	'-Dxdm-auth-1=false'
	'-Dpciaccess=false'
	'-Dudev=false'
	'-Dudev_kms=false'
	'-Ddpms=false'
	'-Dscreensaver=false'
	'-Dxres=false'
	'-Dxinerama=false'
	'-Dxv=false'
	'-Dxvmc=false'
	'-Dxf86-input-inputtest=false'
	'-Ddrm=false'
)
depends=(
    libsha1
    libX11
    libxcvt
    libXfont2
    libxkbfile
    pixman 
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

