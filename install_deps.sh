#!/bin/sh

set -x -e

. /etc/os-release

if [ "${ID_LIKE}" = debian ]
then
	sudo apt-get update
	sudo apt-get install -y \
		 git clang cmake libc++-dev libc++abi-dev \
		 libeigen3-dev libboost-dev libboost-thread-dev libboost-system-dev libatlas-base-dev libsuitesparse-dev libblas-dev libglfw3-dev

	old_pwd="${PWD}"
	mkdir -p opencv
	cd opencv
	git clone --branch 3.4.6 https://github.com/opencv/opencv/
	git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/
	cmake -DOPENCV_EXTRA_MODULES_PATH=opencv_contrib/modules opencv
	make -j `getconf _NPROCESSORS_ONLN`
	sudo make install
	cd "${old_pwd}"
else
	echo "${0} does not support ${ID_LIKE} yet."
	exit 1
fi
