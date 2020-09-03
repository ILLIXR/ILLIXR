#!/bin/bash

git clone https://github.com/KhronosGroup/OpenXR-SDK.git "${temp_dir}/OpenXR-SDK"
echo "TEST"
cmake -S "${temp_dir}/OpenXR-SDK" -B "${temp_dir}/OpenXR-SDK/build"
sudo make -C "${temp_dir}/OpenXR-SDK/build" "-j$(nproc)" install
