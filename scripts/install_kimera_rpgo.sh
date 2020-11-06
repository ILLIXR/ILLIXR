#!/bin/bash

git clone https://github.com/MIT-SPARK/Kimera-RPGO.git "${temp_dir}/Kimera-RPGO"

cmake \
	-S "${temp_dir}/Kimera-RPGO" \
	-B "${temp_dir}/Kimera-RPGO/build"

sudo make -j "${temp_dir}/Kimera-RPGO/build" $(nproc) install
