#!/bin/bash

### Extra instructions for docker ###
# export DEBIAN_FRONTEND=noninteractive TZ=America/Chicago
# apt update && apt install -y sudo

### Normalize environment ###

set -e
cd "$(dirname "${0}")"

### Parse args ###

assume_yes=
while [[ "$#" -gt 0 ]]; do
    case "${1}" in
        -y|--yes) assume_yes=true ;;
        *) echo "Unknown parameter passed: ${1}"; exit 1 ;;
    esac
    shift
done

### Get OS ###

. /etc/os-release

### Helper functions ###

function y_or_n() {
	if [ -n "${assume_yes}" ]; then
		return 0
	else
		while true; do
			echo "${1}"
			read -rp "Yes or no? " yn
			case "${yn}" in
				[Yy]* ) return 0 ;;
				[Nn]* ) echo "Declined."; return 1 ;;
				* ) echo "Please answer yes or no." ;;
			esac
		done
	fi
}

### Main ###

if [ "${ID_LIKE}" = debian ] || [ "${ID}" = debian ]
then
	# For system-wide installs that are not possible via apt
	temp_dir=/tmp/ILLIXR_deps
	mkdir -p "${temp_dir}"

	# For local installs
	opt_dir=/opt/ILLIXR
	sudo mkdir -p "${opt_dir}"
	sudo chown $USER: "${opt_dir}"

	if y_or_n "Next: Add apt-get sources list/keys and install necessary packages"; then
		. ./scripts/install_apt_deps.sh
	fi

	if [ ! -d "${temp_dir}/opencv" ] && y_or_n "Next: Install OpenCV from source"; then
		. ./scripts/install_opencv.sh
	fi

	if [ ! -d "${temp_dir}/Vulkan-Headers" ] && y_or_n "Next: Install Vulkan Headers from source"; then
		. ./scripts/install_vulkan_headers.sh
	fi

	if [ ! -d "${opt_dir}/googletest" ] && y_or_n "Next: Install gtest"; then
		. ./scripts/install_gtest.sh
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

	if [ ! -d "${temp_dir}/OpenXR-SDK" ] && y_or_n "Next: Install OpenXR SDK from souce"; then
		. ./scripts/install_openxr.sh
	fi

	if ! which conda 2> /dev/null; then
		if [ ! -d "$HOME/miniconda3" ]; then
			if y_or_n "Next: Install Conda"; then
				. ./scripts/install_conda.sh
			fi
		fi
	fi

	# I won't ask the user first, because this is not a global installation.
	# All of this stuff goes into a project-specific venv.
	$HOME/miniconda3/bin/conda env create --force -f runner/environment.yml
else
	echo "${0} does not support ${ID_LIKE} yet."
	exit 1
fi
