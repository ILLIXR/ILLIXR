#!/bin/bash


### Initial setup ###

# Source environment variables describing the current OS distribution
. /etc/os-release


### Helper functions ###

# Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }

# Function for flattening a string with items seperated by new lines
# and white space to a single line seperated by a single space.
# When calling flatten list, the multi-line list (string) passed as
# the first argument must be protected by double quotes when expanding
# a variable.
# Good sample call site: flat_string=$(flatten_list "${multi_line_string}")
# Bad sample call site:  flat_string="$(flatten_list ${multi_line_string})"
function flatten_list() { echo "${1}" | xargs; }

# Add a repository and the necessary keys required given a list of key servers
# and a repository url.
# If a key ID is provided, use that key ID to search each key server.
# If successful, only one key is added per call (one server used).
function add_repo() {
    key_srv_url_list=${1}
    repo_url=${2}
    key_id=${3}
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
    sudo add-apt-repository -u -y "deb ${repo_url} ${VERSION_CODENAME} main"
}


### Package dependencies setup ###

# List of common package dependencies for the ILLIXR project,
# grouped by purpose/feature

pkg_dep_list_prereq="
    curl
    gnupg2
    software-properties-common
" # End list

pkg_dep_list_common="
    build-essential
    git
    clang-10
    make
    cmake
    unzip
    wget
    pkg-config
" # End list

pkg_dep_list_gl="
    glew-utils
    glslang-tools
    freeglut3-dev
    libglew-dev
    libglfw3-dev
    libvirglrenderer-dev
" # End list

pkg_dep_list_mesa="
    mesa-common-dev
    libglu1-mesa-dev
    libdrm-dev
" # End list

pkg_dep_list_display="
    xvfb
    libx11-xcb-dev
    libx11-dev
    libxcb-glx0-dev
    libxcb-randr0-dev
    libxrandr-dev
    libxkbcommon-dev
    libwayland-dev
" # End list

pkg_dep_list_image="
    libjpeg-dev
    libpng-dev
    libtiff-dev
    libvtk6-dev
" # End list

pkg_dep_list_sound="
    libsdl2-dev
    libpulse-dev
" # End list

pkg_dep_list_usb="
    libusb-dev
    libusb-1.0
    libudev-dev
    libv4l-dev
    libhidapi-dev
" # End list

pkg_dep_list_thread="
    libc++-dev
    libc++abi-dev
    libboost-all-dev
    libtbb-dev
" # End list

pkg_dep_list_math="
    gfortran
    libeigen3-dev
    libblas-dev
    libsuitesparse-dev
    libparmetis-dev
    libatlas-base-dev
" # End list

pkg_dep_list_nogroup="
    libsqlite3-dev
    libepoxy-dev
    libgbm-dev
    libgtest-dev
    libgtk2.0-dev
    libgtk-3-dev
    libgflags-dev
    libgoogle-glog-dev
    libssl-dev
" # End list

pkg_dep_list_realsense="
    librealsense2-dkms
    librealsense2-utils
" # End List

# List of package dependency group names (not including group 'prereq'
# and other groups added later based on need/support)
pkg_dep_groups="common gl mesa display image sound usb thread math nogroup"


### Intel RealSense package dependencies setup ###

pkg_install_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/distribution_linux.md"
pkg_build_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/installation.md"
pkg_warn_msg_realsense="Currently, Intel RealSense does not support binary package installations for Ubuntu 20 LTS kernels, or other non-Ubuntu Linux distributions. If your project requires Intel RealSense support, please build and install the Intel RealSense SDK from source. For more information, visit '${pkg_install_url_realsense}' and '${pkg_build_url_realsense}'."

# Check for distribution support of Intel RealSense
# For supported distributions, automatically add the required install
# package dependencies
echo "Detected OS: '${PRETTY_NAME}'"
if [ "${ID}" == "ubuntu" ] && [ "${VERSION_ID}" == "18.04" ]; then
    use_realsense="yes"
    pkg_dep_groups+=" realsense"
else
    use_realsense="no"
    print_warning "${pkg_warn_msg_realsense}"
fi


### Package dependencies installation ###

# Generate the list of package dependencies to install based on groups
pkg_dep_list=""
for group in ${pkg_dep_groups}; do
    pkg_dep_list_group_var="pkg_dep_list_${group}"
    pkg_dep_list_group=$(flatten_list "${!pkg_dep_list_group_var}")
    pkg_dep_list+="${pkg_dep_list_group} "
done

# Print all packages marked for installation
echo -n "Packages marked for installation: "
echo -n $(flatten_list "${pkg_dep_list_prereq}")
echo " ${pkg_dep_list}"

# Refresh package list and grab prerequisite packages needed for package
# and repository management within this script
sudo apt-get update
sudo apt-get install -y $(flatten_list "${pkg_dep_list_prereq}")

# Add Kitware repository (for third party Ubuntu dependencies)
key_srv_url_kitware="https://apt.kitware.com/keys/kitware-archive-latest.asc"
repo_url_kitware="https://apt.kitware.com/ubuntu"
add_repo "${key_srv_url_kitware}" "${repo_url_kitware}"

# If supported, add the gpg keys and repository for Intel RealSense
if [ "${use_realsense}" == "yes" ]; then
    key_srv_url_list_realsense="keys.gnupg.net hkp://keyserver.ubuntu.com:80"
    repo_url_realsense="http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo"
    key_id_realsense="F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE"
    add_repo "${key_srv_url_list_realsense}" "${repo_url_realsense}" "${key_id_realsense}"
fi

# Add repositories needed for drivers and miscellaneous dependencies (python)
sudo add-apt-repository -u -y ppa:graphics-drivers/ppa
sudo add-apt-repository -u -y ppa:deadsnakes/ppa

# Install all packages marked by this script
sudo apt-get install -y ${pkg_dep_list}
