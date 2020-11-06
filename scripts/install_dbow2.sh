#!/bin/bash

git clone https://github.com/dorian3d/DBoW2.git "${temp_dir}/DBoW2"

cmake \
	-S "${temp_dir}/DBoW2" \
	-B "${temp_dir}/DBoW2/build"

sudo make -j "${temp_dir}/DBoW2/build" $(nproc) install
