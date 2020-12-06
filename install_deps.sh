#!/bin/bash

### Extra instructions for docker ###
# export DEBIAN_FRONTEND=noninteractive TZ=America/Chicago
# apt update && apt install -y sudo

### Normalize environment ###

set -e
cd "$(dirname "${0}")"

### Parse args ###
show_help=0
exit_code=
assume_yes=

# Set nproc to either 1 or half the available cores
illixr_nproc=1
while [[ "$#" -gt 0 ]]; do
    case "${1}" in
        -y|--yes) assume_yes=true ;;
        -h|--help)
            show_help=1
            exit_code=1
            ;;
        -j|--jobs)
            # Checks if the number of jobs is given and not negative
            if [ -n "$2" ] && [ ${2:0:1} != "-" ]; then
                illixr_nproc=$2
            else
                echo "Error: Argument for number of jobs is missing or negative"
                show_help=1
                exit_code=1
            fi
            shift
            ;;
        *)  echo "Error: Unknown parameter passed: ${1}"; show_help=1; exit_code=1 ;;
    esac
    shift
done

if  [[ "$show_help" -eq 1 ]]; then
    echo "ILLIXR install_deps:"
    echo "    -y/--yes - yes to all dependencies"
    echo "    -j/--jobs - number of jobs/cores/threads for make to use"
    echo "    -h/--help - help info for install_deps"
    exit $exit_code
fi

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

    echo "* The user will now be prompted to install the following dependencies and optional features:"
    echo "  Binary packages (apt-get), Docker, OpenCV, Vulkan, gtest, qemu, OpenXR-SDK, gtsam, opengv, DBoW2, Kimera-RPGO, Conda (miniconda3)"

    if y_or_n "Next: Add apt-get sources list/keys and install necessary packages"; then
        if y_or_n "^^^^  Also install Docker (docker-ce) for local CI/CD debugging support"; then
            export use_docker="yes"
        fi
        pmt_msg_warn_cuda="Also automate install of CUDA 11 (cuda) for GPU plugin support on Ubuntu (_only_!)"
        pmt_msg_warn_cuda+="\n(This script will _not_ install the package on non-Ubuntu distributions, or if a supported GPU is not found)"
        if y_or_n "^^^^  ${pmt_msg_warn_cuda}"; then
            export use_cuda="yes"
        fi
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

    if [ ! -d "${opt_dir}/qemu" ] && y_or_n "Next: Install qemu (not necessary for core ILLIXR; necessary for virtualization)"; then
        . ./scripts/install_qemu.sh
    fi

    # if [ ! -d Vulkan-Loader ]; then
    #   echo "Next: Install Vulkan Loader from source"
    #   if y_or_n; then
    #       git clone https://github.com/KhronosGroup/Vulkan-Loader.git
    #       mkdir -p Vulkan-Headers/build && cd Vulkan-Headers/build
    #       ../scripts/update_deps.py
    #       cmake -C helper.cmake ..
    #       cmake --build .
    #       cd ../..
    #   fi
    # fi

    if [ ! -d "${temp_dir}/OpenXR-SDK" ] && y_or_n "Next: Install OpenXR SDK from source"; then
        . ./scripts/install_openxr.sh
    fi

    if [ ! -d "${opt_dir}/gtsam" ] && y_or_n "Next: Install gtsam from source"; then
        . ./scripts/install_gtsam.sh
    fi

    if [ ! -d "${opt_dir}/opengv" ] && y_or_n "Next: Install opengv from source"; then
        . ./scripts/install_opengv.sh
    fi

    if [ ! -d "${opt_dir}/DBoW2" ] && y_or_n "Next: Install DBoW2 from source"; then
        . ./scripts/install_dbow2.sh
    fi

    if [ ! -d "${opt_dir}/Kimera-RPGO" ] && y_or_n "Next: Install Kimera-RPGO from source"; then
        . ./scripts/install_kimera_rpgo.sh
    fi

    if ! which conda 2> /dev/null; then
        if [ ! -d "$HOME/miniconda3" ]; then
            if y_or_n "Next: Install Conda"; then
                . ./scripts/install_conda.sh
            fi
        fi
    fi

    # Load new library paths
    sudo ldconfig

    # I won't ask the user first, because this is not a global installation.
    # All of this stuff goes into a project-specific venv.
    $HOME/miniconda3/bin/conda env create --force -f runner/environment.yml
else
    echo "${0} does not support ${ID_LIKE} yet."
    exit 1
fi
