#!/bin/bash

#--- Interactive script for installing ILLIXR dependencies ---#


### Setup ###

## Update the working directory
cd $(dirname "${0}")

if [ $(whoami) = "root" ]; then
	echo "Please run this script as your normal user, not as root, not with sudo."
	echo "This script will ask for your password when it wants to elevate privileges."
	exit 1
fi

## Source the default system values
. scripts/default_values.sh

## Get OS
. /etc/os-release

## Parameters and global values
enable_quiet="no"
assume_yes="no"
show_help="no"
exit_code=0

help_msg="ILLIXR Dependency Installer:

  ## Quiet output from dependency install scripts
    -q/--quiet

  ## Yes to all dependencies and default value prompts
    -y/--yes
  ** WARNING ** This option may cause this script to _DELETE_ data without supervision

  ## Number of jobs/cores/threads for make to use (default: '${illixr_nproc}')
    -j/--jobs <integer>

  ## Build type for dependencies using cmake (default: '${build_type}')
    -b/--build-type [Release|RelWithDebInfo|Debug]

  ## Show help info for this script ('${0}') and quit
    -h/--help
" # End help_msg


### Helper functions ###

## Source the global helper functions
. scripts/system_utils.sh

function y_or_n()
{
    if [ "${assume_yes}" = "yes" ]; then
        return 0
    else
        while true; do
            echo -e -n "#>  ${1}"
            read -rp " Yes or no? " yn
            case "${yn}" in
                [Yy]*)  return 0 ;;
                [Nn]*)  echo "Declined."; return 1 ;;
                * )     echo "Please answer yes or no." ;;
            esac
        done
    fi
}

function prompt_value()
{
    local name=${1}
    local value_default=${2}

    local value_in=""

    if [ "${assume_yes}" = "yes" ]; then
        value_out="${value_default}"
    else
        echo -n "#>  Enter '${name}' [default: '${value_default}']: "
        read -rp '' value_in

        if [ -z "${value_in}" ]; then
            value_out="${value_default}"
        else
            value_out="${value_in}"
        fi
    fi

    export value_out
}

function prompt_install()
{
    local dep_name=${1}
    local deps_log_dir=${2}
    local script_path=${3}
    local parent_dir=${4}
    local msg_prompt=${5}
    local dep_ver=${6}

    if y_or_n "Next: ${msg_prompt}"; then
        prompt_value dep_name "${dep_name}"
        dep_name="${value_out}"
        prompt_value parent_dir "${parent_dir}"
        parent_dir="${value_out}"
        prompt_value dep_ver "${dep_ver}"
        dep_ver="${value_out}"

        export src_dir="${parent_dir}/${dep_name}"

        local enable_dry_run="yes"
        detect_dependency "${dep_name}" "${deps_log_dir}" "${enable_dry_run}"
        if [ "$?" -eq 0 ]; then
            echo "Detected previous installation for '${dep_name}'."

            if [ "${enable_quiet}" = "no" ]; then
                tail --lines=5 "${dep_log_path}"
            fi

            if y_or_n "Use the configuration in '${dep_log_path}'?"; then
                ## Load the configuration
                . "${dep_log_path}"

                ## Overwritten variables in 'script_path':
                #> dep_name
                #> src_dir
                #> dep_ver
            fi
        fi

        if [ -d "${src_dir}" ]; then
            echo "Source directory '${src_dir}' already exists."

            if y_or_n "Clear directory and proceed with installation?"; then
                rm -rf --preserve-root "${src_dir}"

                if [ "$?" -eq 1 ]; then
                    echo "Failed to clear '${src_dir}."

                    if y_or_n "Try to clear '${src_dir}' with sudo?"; then
                        sudo rm -rf --preserve-root "${src_dir}"
                    fi
                fi
            else
                return 1
            fi
        fi

        echo "INSTALL [dep <- '${dep_name}', dir <- '${src_dir}']"

        if [ "${enable_quiet}" = "no" ]; then
            . "${script_path}"
        else
            . "${script_path}" >/dev/null
        fi

        return "$?"
    fi

    return 1
}


### Parse args ###

if [ "$#" -eq 0 ]; then
    ## If no arguments are provided, print the help screen and exit
    show_help="yes"
    exit_code=0
else
    while [ "$#" -gt 0 ]; do
        case "${1}" in
            -q | --quiet)
                enable_quiet="yes"
                ;;
            -y | --yes)
                assume_yes="yes"
                ;;
            -h | --help)
                show_help="yes"
                exit_code=1
                ;;
            -j | --jobs)
                ## Checks if the number of jobs is given and not negative
                if [ -n "${2}" ] && [ ${2:0:1} != "-" ]; then
                    illixr_nproc="${2}"
                else
                    print_warning "Argument for number of jobs is missing or negative"
                    show_help="yes"
                    exit_code=1
                fi
                shift
                ;;
            -b | --build-type)
                case "${2}" in
                    Release | RelWithDebInfo | Debug)
                        export build_type="${2}"
                        ;;
                    *)
                        print_warning "Argument for build type not [Release|RelWithDebInfo|Debug]"
                        ;;
                esac
                shift
                ;;
            *)
                echo "Error: Unknown parameter passed: ${1}"
                show_help="yes"
                exit_code=1
                ;;
        esac
        shift
    done
fi

if [ "${show_help}" = "yes" ]; then
    echo -e "${help_msg}"

    if [ "${exit_code}" -ne 0 ]; then
        exit "${exit_code}"
    fi
fi


### Main ###

if [ ! "${ID_LIKE}" = debian ] && [ ! "${ID}" = debian ]; then
    print_warning "${0} does not support '${ID_LIKE}'/'${ID}' yet."
    exit 1
fi

## For system-wide or from-source installs that are not possible via apt
sudo mkdir -p "${opt_dir}"
sudo chown "${USER}:" "${opt_dir}"

## Source the configurations for our dependencies
. deps.sh

echo "The user will now be prompted to install the following dependencies and optional features:
  Binary packages (via apt-get), Docker, CUDA, OpenCV, Vulkan,
  gtest, qemu, OpenXR-SDK, gtsam, opengv, DBoW2, Kimera-RPGO, Conda (miniconda3), DepthAI
" # End echo

if y_or_n "Add apt-get sources list/keys and install necessary packages"; then
    if y_or_n "^^^^  Also install Docker (docker-ce) for local CI/CD debugging support"; then
        export use_docker="yes"
    fi
    pmt_msg_warn_cuda="Also automate install of CUDA 11 (cuda) for GPU plugin support on Ubuntu (_only_!)"
    pmt_msg_warn_cuda+="\n(This script will _not_ install the package on non-Ubuntu distributions, "
    pmt_msg_warn_cuda+="or if a supported GPU is not found)"
    if y_or_n "^^^^  ${pmt_msg_warn_cuda}"; then
        export use_cuda="yes"
    fi

    if [ "${enable_quiet}" = "no" ]; then
        . "${script_path_apt}"
    else
        . "${script_path_apt}" >/dev/null
    fi
fi

## Locally built clang not in use yet
#prompt_install \
#    "${dep_name_clang}" \
#    "${deps_log_dir}" \
#    "${script_path_clang}" \
#    "${parent_dir_clang}" \
#    "${dep_prompt_clang}" \
#    "${dep_ver_clang}"

## Locally built boost not in use yet
#prompt_install \
#    "${dep_name_boost}" \
#    "${deps_log_dir}" \
#    "${script_path_boost}" \
#    "${parent_dir_boost}" \
#    "${dep_prompt_boost}" \
#    "${dep_ver_boost}"

prompt_install \
    "${dep_name_opencv}" \
    "${deps_log_dir}" \
    "${script_path_opencv}" \
    "${parent_dir_opencv}" \
    "${dep_prompt_opencv}" \
    "${dep_ver_opencv}"

## Locally built eigen not in use yet
#prompt_install \
#    "${dep_name_eigen}" \
#    "${deps_log_dir}" \
#    "${script_path_eigen}" \
#    "${parent_dir_eigen}" \
#    "${dep_prompt_eigen}" \
#    "${dep_ver_eigen}"

prompt_install \
    "${dep_name_vulkan}" \
    "${deps_log_dir}" \
    "${script_path_vulkan}" \
    "${parent_dir_vulkan}" \
    "${dep_prompt_vulkan}" \
    "${dep_ver_vulkan}"

prompt_install \
    "${dep_name_gtest}" \
    "${deps_log_dir}" \
    "${script_path_gtest}" \
    "${parent_dir_gtest}" \
    "${dep_prompt_gtest}" \
    "${dep_ver_gtest}"

prompt_install \
    "${dep_name_qemu}" \
    "${deps_log_dir}" \
    "${script_path_qemu}" \
    "${parent_dir_qemu}" \
    "${dep_prompt_qemu}" \
    "${dep_ver_qemu}"

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

prompt_install \
    "${dep_name_openxr}" \
    "${deps_log_dir}" \
    "${script_path_openxr}" \
    "${parent_dir_openxr}" \
    "${dep_prompt_openxr}" \
    "${dep_ver_openxr}"

prompt_install \
    "${dep_name_gtsam}" \
    "${deps_log_dir}" \
    "${script_path_gtsam}" \
    "${parent_dir_gtsam}" \
    "${dep_prompt_gtsam}" \
    "${dep_ver_gtsam}"

prompt_install \
    "${dep_name_opengv}" \
    "${deps_log_dir}" \
    "${script_path_opengv}" \
    "${parent_dir_opengv}" \
    "${dep_prompt_opengv}" \
    "${dep_ver_opengv}"

prompt_install \
    "${dep_name_dbow2}" \
    "${deps_log_dir}" \
    "${script_path_dbow2}" \
    "${parent_dir_dbow2}" \
    "${dep_prompt_dbow2}" \
    "${dep_ver_dbow2}"

prompt_install \
    "${dep_name_kimera_rpgo}" \
    "${deps_log_dir}" \
    "${script_path_kimera_rpgo}" \
    "${parent_dir_kimera_rpgo}" \
    "${dep_prompt_kimera_rpgo}" \
    "${dep_ver_kimera_rpgo}"

prompt_install \
    "${dep_name_conda}" \
    "${deps_log_dir}" \
    "${script_path_conda}" \
    "${parent_dir_conda}" \
    "${dep_prompt_conda}" \
    "${dep_ver_conda}"

prompt_install \
    "${dep_name_depthai}" \
    "${deps_log_dir}" \
    "${script_path_depthai}" \
    "${parent_dir_depthai}" \
    "${dep_prompt_depthai}" \
    "${dep_ver_depthai}"

prompt_install \
    "${dep_name_openvins}" \
    "${deps_log_dir}" \
    "${script_path_openvins}" \
    "${parent_dir_openvins}" \
    "${dep_prompt_openvins}" \
    "${dep_ver_openvins}"

## Load new library paths
sudo ldconfig

### Virtual environment creation ###

echo "Attempting to create a virtual environment configuration via conda ..."

## Check for a previous conda installation
detect_dependency "${dep_name_conda}" "${deps_log_dir}"
if [ "$?" -eq 0 ]; then
    dep_log_path_conda="${dep_log_path}"
    src_dir_conda="${src_dir}"
    echo "Found conda installation log '${dep_log_path_conda}' : dir <- '${src_dir_conda}'"
else
    dep_log_path_conda="${dep_log_path}"
    src_dir_conda="${parent_dir_conda}/${dep_name_conda}"

    dep_missing_msg_conda="Installation log for conda not found at '${dep_log_path_conda}'."
    dep_missing_msg_conda+="\n  Conda may have been installed without this script (or an older version)."

    print_warning "${dep_missing_msg_conda}"

    if ! y_or_n "Try to create a Python environment configuration anyway?"; then
        echo "This was the last step. Exiting early."
        exit 0
    fi

    ## Conda may have been installed without this script
    echo "Assuming : dir <- '${src_dir_conda}'"
fi

env_config_parent_dir=$(dirname "${env_config_path}")
if [ ! -d "${env_config_parent_dir}" ]; then
    mkdir -p "${env_config_parent_dir}"
fi

cmd_conda="${src_dir_conda}/bin/conda"
if [ -f "${cmd_conda}" ]; then
    echo "Found a manual conda installation. Creating a project-specific virtual environment."
    "${cmd_conda}" env create --force -f "${env_config_path}"
else
    echo "Trying a system conda installation. Creating a project-specific virtual environment."
    conda env create --force -f "${env_config_path}" 2>/dev/null
fi

exit 0
