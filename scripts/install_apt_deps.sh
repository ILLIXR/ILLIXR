#!/bin/bash


### Default variables setup ###

if [ -z "${use_realsense}" ]; then
    use_realsense="no"
fi
if [ -z "${use_docker}" ]; then
    use_docker="no"
fi


### Initial setup ###

# Source environment variables describing the current OS distribution
. /etc/os-release
echo "Detected OS: '${PRETTY_NAME}'"


### Helper functions ###

# Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }

# Add a repository and the necessary keys required given a list of key servers,
# a repository url, and a repository component name
# If a key ID is provided, use that key ID to search each key server
# If successful, only one key is added per call (one server used)
function add_repo() {
    local key_srv_url_list=${1}
    local repo_url=${2}
    local repo_comp=${3}
    local key_id=${4}
    for key_srv_url in ${key_srv_url_list}; do
        if [ -z "${key_id}" ]; then
            curl "${key_srv_url}" | sudo apt-key add -
        else
            sudo apt-key adv --keyserver "${key_srv_url}" --recv-key "${key_id}"
        fi
        if [ "${?}" -eq "0" ]; then
            break # Stop adding keys after the first success
        fi
    done
    sudo add-apt-repository -u -y "deb ${repo_url} ${VERSION_CODENAME} ${repo_comp}"
}

# Generate the list of package dependencies to install based on the provided package groups
# The indirect variables referenced by the group names should be bash arrays
# Good call site: list=$(pkg_dep_list_from "${group_list}")
# Bad call site:  list="$(pkg_dep_list_from ${group_list})"
function pkg_dep_list_from() {
    local pkg_dep_list=""
    local pkg_dep_groups=${1}
    for group in ${pkg_dep_groups}; do
        pkg_dep_list_group_var="pkg_dep_list_${group}"
        pkg_dep_list_group="${pkg_dep_list_group_var}[@]"
        pkg_dep_list+="${!pkg_dep_list_group} "
    done
    echo "${pkg_dep_list}"
}


### Package dependencies setup ###

# List of common package dependencies for the ILLIXR project,
# grouped by purpose/feature

pkg_dep_list_prereq=(
    curl
    gnupg2
    software-properties-common
) # End list

pkg_dep_list_common=(
    build-essential
    git
    clang-10
    make
    cmake
    unzip
    wget
    pkg-config
) # End list

pkg_dep_list_gl=(
    glew-utils
    glslang-tools
    freeglut3-dev
    libglew-dev
    libglfw3-dev
    libvirglrenderer-dev
) # End list

pkg_dep_list_mesa=(
    mesa-common-dev
    libglu1-mesa-dev
    libdrm-dev
) # End list

pkg_dep_list_display=(
    xvfb
    libx11-xcb-dev
    libx11-dev
    libxcb-glx0-dev
    libxcb-randr0-dev
    libxrandr-dev
    libxkbcommon-dev
    libwayland-dev
) # End list

pkg_dep_list_image=(
    libjpeg-dev
    libpng-dev
    libtiff-dev
    libvtk6-dev
) # End list

pkg_dep_list_sound=(
    libpulse-dev
) # End list

pkg_dep_list_usb=(
    libusb-dev
    libusb-1.0
    libudev-dev
    libv4l-dev
    libhidapi-dev
) # End list

pkg_dep_list_thread=(
    libc++-dev
    libc++abi-dev
    libboost-all-dev
    libtbb-dev
) # End list

pkg_dep_list_math=(
    gfortran
    libeigen3-dev
    libblas-dev
    libsuitesparse-dev
    libparmetis-dev
    libatlas-base-dev
) # End list

pkg_dep_list_nogroup=(
    libsqlite3-dev
    libepoxy-dev
    libgbm-dev
    libgtest-dev
    libgtk2.0-dev
    libgtk-3-dev
    libgflags-dev
    libgoogle-glog-dev
    libssl-dev
    libsdl2-dev
    mkdocs
) # End list

# List of selected/optional package dependencies for the ILLIXR project,
# grouped by purpose/feature

pkg_dep_list_prereq_docker=(
    apt-transport-https
    ca-certificates
) # End List

pkg_dep_list_docker=(
    docker-ce
) # End List

pkg_dep_list_realsense=(
    librealsense2-dkms
    librealsense2-utils
) # End List

# List of package dependency group names (for prerequisites and other
# packages added later based on need/support)
pkg_dep_groups_prereq="prereq"
pkg_dep_groups="common gl mesa display image sound usb thread math nogroup"


### Selected and optional package dependencies setup ###

## Docker ##

# If prompted to use docker, add the docker's package groups to our lists
if [ "${use_docker}" == "yes" ]; then
    pkg_dep_groups_prereq+=" prereq_docker"
    pkg_dep_groups+=" docker"
fi

## Intel RealSense ##

# Check for distribution support of Intel RealSense
# For supported distributions, automatically add the RealSense package group to our list
pkg_install_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/distribution_linux.md"
pkg_build_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/installation.md"
pkg_warn_msg_realsense="Currently, Intel RealSense does not support binary package installations for Ubuntu 20 LTS kernels, or other non-Ubuntu Linux distributions. If your project requires Intel RealSense support, please build and install the Intel RealSense SDK from source. For more information, visit '${pkg_install_url_realsense}' and '${pkg_build_url_realsense}'."
if [ "${ID}" == "ubuntu" ] && [ "${VERSION_ID}" == "18.04" ]; then
    use_realsense="yes"
    pkg_dep_groups+=" realsense"
else
    print_warning "${pkg_warn_msg_realsense}"
fi


### Prerequisite package depenencies and repository setup ###

# Generate the list of package dependencies to install based on prerequisite
# package groups
pkg_dep_list=$(pkg_dep_list_from "${pkg_dep_groups_prereq}")
echo "Installing prerequisite packages: ${pkg_dep_list}"

# Refresh package list and grab prerequisite packages needed for package
# and repository management within this script
sudo apt-get update
sudo apt-get install -q -y ${pkg_dep_list}

# Add Kitware repository (for third party Ubuntu dependencies)
key_srv_url_kitware="https://apt.kitware.com/keys/kitware-archive-latest.asc"
repo_url_kitware="https://apt.kitware.com/ubuntu"
add_repo "${key_srv_url_kitware}" "${repo_url_kitware}" "main"

# If prompted, add Docker repository (for local CI/CD debugging)
if [ "${use_docker}" == "yes" ]; then
    key_srv_url_docker="https://download.docker.com/linux/ubuntu/gpg"
    repo_url_docker="https://download.docker.com/linux/ubuntu"
    add_repo "${key_srv_url_docker}" "${repo_url_docker}" "stable"
fi

# If supported, add the gpg keys and repository for Intel RealSense
if [ "${use_realsense}" == "yes" ]; then
    key_srv_url_list_realsense="keys.gnupg.net hkp://keyserver.ubuntu.com:80"
    repo_url_realsense="http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo"
    key_id_realsense="F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE"
    add_repo "${key_srv_url_list_realsense}" "${repo_url_realsense}" "main" "${key_id_realsense}"
fi

# Add repositories needed for drivers and miscellaneous dependencies (python)
sudo add-apt-repository -u -y ppa:graphics-drivers/ppa
sudo add-apt-repository -u -y ppa:deadsnakes/ppa


### General package dependencies installation ###

# Generate and print all packages marked for installation
pkg_dep_list=$(pkg_dep_list_from "${pkg_dep_groups}")
echo "Packages marked for installation: ${pkg_dep_list}"

# Install all packages marked by this script
sudo apt-get install -q -y ${pkg_dep_list}
