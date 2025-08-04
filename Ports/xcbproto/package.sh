#!/usr/bin/env -S bash ../.port_include.sh
port='xcbproto'
version='1.17.0'
files=(
    "https://x.org/releases/individual/xcb/xcb-proto-${version}.tar.xz#2c1bacd2110f4799f74de6ebb714b94cf6f80fb112316b1219480fd22562148c"
)
workdir="xcb-proto-${version}"
useconfigure='true'
