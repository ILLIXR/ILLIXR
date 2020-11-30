#!/bin/bash

git clone https://github.com/MIT-SPARK/Kimera-RPGO.git "${opt_dir}/Kimera-RPGO"

cmake \
	-S "${opt_dir}/Kimera-RPGO" \
	-B "${opt_dir}/Kimera-RPGO/build"

sudo make -C "${opt_dir}/Kimera-RPGO/build" "-j${illixr_nproc}" install
