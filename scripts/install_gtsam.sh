#!/bin/bash

git clone -b master https://github.com/borglab/gtsam.git "${temp_dir}/gtsam"
cmake -S "${temp_dir}/gtsam" -B "${temp_dir}/gtsam/build"
sudo make -C "${temp_dir}/gtsam/build" "-j$(nproc)" install