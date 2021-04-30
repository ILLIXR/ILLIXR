#!/bin/bash

#--- Script for installing binary packages via apt ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source environment variables describing the current OS distribution
. /etc/os-release

## Collect system and distribution information
kernel_version="$(uname -r)"
arch_name="$(uname -m)"
distro_version="${VERSION_ID}"
distro_name="${ID}"
distro_codename="${VERSION_CODENAME}"

echo "Detected OS: '${PRETTY_NAME}' on kernel version '${kernel_version}' on arch '${arch_name}'"


### Helper functions ###

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
            sudo apt-key adv --fetch-keys "${key_srv_url}"
        else
            sudo apt-key adv --keyserver "${key_srv_url}" --recv-key "${key_id}"
        fi
        if [ "${?}" -eq "0" ]; then
            break # Stop adding keys after the first success
        fi
    done
    sudo add-apt-repository -u -y "deb ${repo_url} ${repo_comp}"
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
        pkg_dep_list+=" ${!pkg_dep_list_group}"
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
    bc
    pciutils
) # End list

pkg_dep_list_common=(
    build-essential
    git
    #clang-10
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
    libportaudio2
    libportaudiocpp0
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
    #libboost-all-dev
    libtbb-dev
) # End list

pkg_dep_list_math=(
    gfortran
    libblas-dev
    libsuitesparse-dev
    libparmetis-dev
    libatlas-base-dev
    libeigen3-dev
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

pkg_dep_list_realsense_anyarch=(
    librealsense2-utils
    librealsense2-dev
) # End List

pkg_dep_list_realsense_x86_64=(
    librealsense2-dkms
) # End List

pkg_dep_list_prereq_cuda=(
    linux-headers-${kernel_version}
) # End List

pkg_dep_list_cuda=(
    cuda-11-1
) # End List

# List of package dependency group names (for prerequisites and other
# packages added later based on need/support)
pkg_dep_groups_prereq="prereq"
pkg_dep_groups="common gl mesa display image sound usb thread math nogroup"


### Package dependencies and repository setup ###

# Generate the list of package dependencies to install based on prerequisite
# package groups
pkg_dep_list=$(pkg_dep_list_from "${pkg_dep_groups_prereq}")
echo "Installing prerequisite packages: ${pkg_dep_list}"

# Refresh package list and grab prerequisite packages needed for package
# and repository management within this script
sudo apt-get update
sudo apt-get install -q -y ${pkg_dep_list}


### Selected and optional package dependencies setup ###

## Docker ##

# If prompted to use docker, add the docker's package groups to our lists
if [ "${use_docker}" = "yes" ]; then
    pkg_dep_groups_prereq+=" prereq_docker"
    pkg_dep_groups+=" docker"
fi

## Intel RealSense ##

# Check for distribution support of Intel RealSense
# For supported distributions, automatically add the RealSense package group to our list
if [ "${distro_name}" = "ubuntu" ] && [ "${distro_version}" = "18.04" ]; then
    case "${arch_name}" in
        x86_64)     use_realsense="yes"
                    pkg_dep_groups+=" realsense_anyarch"
                    pkg_dep_groups+=" realsense_x86_64"
                    ;;
        aarch64)    use_realsense="yes"
                    pkg_dep_groups+=" realsense_anyarch"
                    ;;
        *)          print_warning "Unsupported arch '${arch_name}' for Intel RealSense."
                    exit 1
                    ;;
    esac
else
    pkg_install_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/distribution_linux.md"
    pkg_build_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/installation.md"
    pkg_warn_msg_realsense="Currently, Intel RealSense does not support binary package installations "
    pkg_warn_msg_realsense+="for Ubuntu 20 LTS kernels, or other non-Ubuntu Linux distributions. "
    pkg_warn_msg_realsense+="If your project requires Intel RealSense support, "
    pkg_warn_msg_realsense+="please build and install the Intel RealSense SDK from source. "
    pkg_warn_msg_realsense+="For more information, visit '${pkg_install_url_realsense}' and '${pkg_build_url_realsense}'."
    print_warning "${pkg_warn_msg_realsense}"
fi

## CUDA ##

# If CUDA is prompted for installation, check for a CUDA compatible NVIDIA GPU
# Disable installation if not supported (only Ubuntu 18 and 20 for now) or no hardware is detected
# CUDA will not be reinstalled if an existing installation is detected
if [ "${use_cuda}" = "yes" ]; then
    is_atleast_ubuntu1804="$(echo "${distro_version} >= 18.04" | bc -l)"

    if [ "${distro_name}" = "ubuntu" ] && [ "${is_atleast_ubuntu1804}" ] && [ "${arch_name}" = "x86_64" ]; then
        case "${distro_version}" in
            21.04)  distro_name_cuda="ubuntu2104"
		    ;;
            20.04)  distro_name_cuda="ubuntu2004"
                    ;;
            18.04)  distro_name_cuda="ubuntu1804"
                    ;;
            *)      print_warning "Bad distribution version '${distro_version}' for CUDA."
                    exit 1
                    ;;
        esac
        case "${arch_name}" in
            #aarch64)    arch_name_cuda="arm64"
            #            ;;
            x86_64)     arch_name_cuda="x86_64"
                        ;;
            *)          print_warning "Unsupported arch '${arch_name}' for CUDA."
                        exit 1
                        ;;
        esac

        supported_hw_cuda=$(lspci | egrep -i "\<NVIDIA\>")

        if [ ! -z "${supported_hw_cuda}" ]; then
            # Check for CUDA support in the installed drivers
            driver_path_cuda="/proc/driver/nvidia/version"
            if [ -e "${driver_path_cuda}" ]; then
                check_driver_cuda=$(grep -i cuda "${driver_path_cuda}" 2>/dev/null)
                if [ "$?" -ne 0 ] && [ ! -z "${check_driver_cuda}" ]; then
                    driver_found_cuda="yes"
                else
                    driver_found_cuda="no"
                fi
            else
                driver_found_cuda="no"
            fi

            # Check for CUDA compiler
            check_compiler_cuda=$(nvcc -V 2>/dev/null)

            # Check if CUDA is already installed. If so, skip CUDA
            if [ "$?" -eq 0 ] || [ "${driver_found_cuda}" = "yes" ]; then
                use_cuda="no"
                driver_info_cuda=$(cat "${driver_path_cuda}")
                pkg_warn_msg_cuda="Detected components of an existing CUDA installation.\n"
                pkg_warn_msg_cuda+="${driver_info_cuda}\nSkipping."
                print_warning "${pkg_warn_msg_cuda}"
            else
                pkg_dep_groups_prereq+=" prereq_cuda"
                pkg_dep_groups+=" cuda"
            fi
        else
            use_cuda="no"
            pkg_support_url_cuda="https://developer.nvidia.com/cuda-gpus"
            pkg_warn_msg_cuda="Unable to detect a CUDA compatible NVIDIA GPU. "
            pkg_warn_msg_cuda+="Check if your hardware is supported at '${pkg_warn_msg_cuda}'."
            print_warning "${pkg_warn_msg_cuda}"
        fi
    else
        use_cuda="no"
        pkg_install_url_cuda="https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#package-manager-installation"
        pkg_warn_msg_cuda="This ILLIXR script currently only supports Ubuntu 18 LTS. "
        pkg_warn_msg_cuda+="See '${pkg_install_url_cuda}' for details on your distribution's install process."
        print_warning "${pkg_warn_msg_cuda}"
    fi
fi


### Package dependencies and repository setup ###

# Add repositories needed for drivers and miscellaneous dependencies (python)
sudo add-apt-repository -u -y ppa:graphics-drivers/ppa
sudo add-apt-repository -u -y ppa:deadsnakes/ppa

# Add Kitware repository (for third party Ubuntu dependencies)
dep_ver_kitware="latest"
key_srv_url_kitware="https://apt.kitware.com/keys/kitware-archive-${dep_ver_kitware}.asc"
repo_url_kitware="https://apt.kitware.com/ubuntu"
add_repo "${key_srv_url_kitware}" "${repo_url_kitware}" "${distro_codename} main"

# If prompted, add Docker repository (for local CI/CD debugging)
if [ "${use_docker}" = "yes" ]; then
    key_srv_url_docker="https://download.docker.com/linux/ubuntu/gpg"
    repo_url_docker="https://download.docker.com/linux/ubuntu"
    add_repo "${key_srv_url_docker}" "${repo_url_docker}" "${distro_codename} stable"
fi

# If supported, add the gpg keys and repository for Intel RealSense
if [ "${use_realsense}" = "yes" ]; then
    key_srv_url_list_realsense="keys.gnupg.net hkp://keyserver.ubuntu.com:80"
    repo_url_realsense="http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo"
    key_id_realsense="F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE"
    add_repo "${key_srv_url_list_realsense}" "${repo_url_realsense}" "${distro_codename} main" "${key_id_realsense}"
fi

# If supported, add the keys and repository for CUDA (for GPU plugin support)
if [ "${use_cuda}" = "yes" ]; then
    repo_url_cuda="https://developer.download.nvidia.com/compute/cuda/repos/${distro_name_cuda}/${arch_name_cuda}"
    key_srv_url_cuda="${repo_url_cuda}/7fa2af80.pub"
    add_repo "${key_srv_url_cuda}" "${repo_url_cuda}" "/"

    path_cmd_cuda='export PATH=/usr/local/cuda-11.1/bin${PATH:+:${PATH}}'
    lib64_cmd_cuda='export LD_LIBRARY_PATH=/usr/local/cuda-11.1/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}'
    pkg_warn_msg_cuda="Before running ILLIXR with plugins using CUDA, "
    pkg_warn_msg_cuda+="make sure to update the following environment variables:\n"
    pkg_warn_msg_cuda+="> ${path_cmd_cuda}\n> ${lib64_cmd_cuda}"
    print_warning "${pkg_warn_msg_cuda}"
fi


### General package dependencies installation ###

# Generate and print all packages marked for installation
pkg_dep_list=$(pkg_dep_list_from "${pkg_dep_groups}")
echo "Packages marked for installation: ${pkg_dep_list}"

# Install all packages marked by this script
sudo apt-get install -q -y ${pkg_dep_list}
