#!/bin/bash

git clone https://github.com/dorian3d/DBoW2.git "${opt_dir}/DBoW2"

cmake \
	-S "${opt_dir}/DBoW2" \
	-B "${opt_dir}/DBoW2/build"

sudo make -C "${opt_dir}/DBoW2/build" "-j${illixr_nproc}" install
