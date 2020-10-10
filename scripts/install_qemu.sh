#!/bin/bash
# Pull latest Qemu
git clone --recursive git://git.qemu.org/qemu.git "${opt_dir}/qemu"
mkdir "${opt_dir}/qemu/build"
cd "${opt_dir}/qemu/build"

# Configure with KVM, OpenGL, SDL
../configure --enable-sdl --enable-opengl --enable-virglrenderer --enable-system --enable-modules --audio-drv-list=pa --target-list=x86_64-softmmu --enable-kvm

# Build
make -j$(nproc --ignore=1)

# Qemu is located at:
# ${opt_dir}/qemu/build/x86_64-softmmu/qemu-system-x86_64
# Go back to where we came from
cd -
