#!/usr/bin/env -S bash ../.port_include.sh
port='libsha1'
version='git'
files=(
    "git+https://github.com/dottedmag/libsha1#cd6a3f811c597126dd932ad6118e65e416f24edf"
)
useconfigure='true'

pre_configure() {
    run autoreconf -i
}

