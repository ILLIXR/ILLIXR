#!/bin/bash

set -x -e

. /etc/os-release

function y_or_n() {
	while true; do
		read -p "Yes or no?" yn
		case $yn in
			[Yy]* ) break;;
			[Nn]* ) return 1;;
			* ) echo "Please answer yes or no.";;
		esac
	done
}

if [ "${ID_LIKE}" = debian -o "${ID}" = debian ]
then
	echo "Next: Add apt-get sources list and keys"
	if y_or_n; then
		sudo add-apt-repository ppa:graphics-drivers/ppa
		wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
		sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ bionic main'
		sudo apt-get update
	fi

	echo "Next: apt-get install necessary packages"
	if y_or_n; then
		sudo apt-get install -y \
			git clang cmake libc++-dev libc++abi-dev \
			libeigen3-dev libboost-all-dev libatlas-base-dev libsuitesparse-dev libblas-dev \
			glslang-tools libsdl2-dev libglu1-mesa-dev mesa-common-dev freeglut3-dev libglew-dev glew-utils libglfw3-dev \
			libusb-dev libusb-1.0 libudev-dev libv4l-dev libhidapi-dev \
			build-essential libx11-xcb-dev libxcb-glx0-dev libxkbcommon-dev libwayland-dev libxrandr-dev \
			libgtest-dev pkg-config libgtk2.0-dev curl
	fi

	old_pwd="${PWD}"
	mkdir -p /tmp/ILLIXR_deps
	cd /tmp/ILLIXR_deps

	if [ ! -d opencv ]; then
		echo "Next: Install OpenCV from source"
		if y_or_n; then
			git clone --branch 3.4.6 https://github.com/opencv/opencv/
			git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/
			mkdir -p opencv/build && cd opencv/build
			cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_JAVA=OFF -DWITH_OPENGL=ON -DOPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules ..
			sudo make -j$(nproc) install
			sudo ldconfig -v
			cd ../..
		fi
	fi

	if [ ! -d Vulkan-Headers ]; then
		echo "Next: Install Vulkan Headers from source"
		if y_or_n; then
			git clone https://github.com/KhronosGroup/Vulkan-Headers.git
			mkdir -p Vulkan-Headers/build && cd Vulkan-Headers/build
			cmake -DCMAKE_INSTALL_PREFIX=install ..
			sudo make -j$(nproc) install
			cd ../..
		fi
	fi

	# if [ ! -d Vulkan-Loader ]; then
	# 	echo "Next: Install Vulkan Loader from source"
	# 	if y_or_n; then
	# 		git clone https://github.com/KhronosGroup/Vulkan-Loader.git
	# 		mkdir -p Vulkan-Headers/build && cd Vulkan-Headers/build
	# 		../scripts/update_deps.py
	# 		cmake -C helper.cmake ..
	# 		cmake --build .
	# 		cd ../..
	# 	fi
	# fi

	if [ ! -d OpenXR-SDK ]; then
		echo "Next: Install OpenXR SDK from souce"
		if y_or_n; then
			git clone https://github.com/KhronosGroup/OpenXR-SDK.git
			mkdir -p OpenXR-SDK/build && cd OpenXR-SDK/build;
			cmake ..
			sudo make -j$(nproc) install
			cd ../..
		fi
	fi

	cd "${old_pwd}"

	if ! poetry; then
		echo "Next: Install Poetry"
		if y_or_n; then
			curl -sSL https://raw.githubusercontent.com/python-poetry/poetry/master/get-poetry.py | python
		fi
	else
		echo "Poetry already installed"
	fi

	# I won't ask the user first, because this is not a global installation.
	# All of this stuff goes into a project-specific venv.
	cd runner
	poetry install
	cd ..
else
	echo "${0} does not support ${ID_LIKE} yet."
	exit 1
fi
