#!/bin/bash

git clone https://github.com/KhronosGroup/Vulkan-Headers.git "${temp_dir}/Vulkan-Headers"
cmake \
	-S "${temp_dir}/Vulkan-Headers" \
	-B "${temp_dir}/Vulkan-Headers/build" \
	-D CMAKE_INSTALL_PREFIX=install
sudo make -C "${temp_dir}/Vulkan-Headers/build" "-j$(nproc)" install
