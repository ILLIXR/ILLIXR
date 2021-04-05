#!/bin/bash

#--- Script defining bash helper functions ---#


## Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }


## Function for logging dependency information post-installation
function log_dependency()
{
    local dep_name=${1}
    local deps_log_dir=${2}
    local src_dir=${3}
    local dep_ver=${4}

    local timestamp=$(date --universal)

    if [ -z "${deps_log_dir}" ]; then
        print_warning "Dependency log directory not defined (deps_log_dir='${deps_log_dir}'.)"
        return 1
    fi

    mkdir -p "${deps_log_dir}"

    local dep_log_path="${deps_log_dir}/${dep_name}.sh"

    if [ ! -f "${dep_log_path}" ]; then
        echo "#!/bin/bash" > "${dep_log_path}"
    fi

    dep_log_msg="\n### INSTALL ###"
    dep_log_msg+="\nexport dep_name='${dep_name}'"
    dep_log_msg+="\nexport src_dir='${src_dir}'"
    dep_log_msg+="\nexport dep_ver='${dep_ver}'"
    dep_log_msg+="\nexport timestamp='${timestamp}'"

    echo -e ${dep_log_msg} >> "${dep_log_path}"

    return 0
}


## Function to detect installation of dependency after logging via 'log_dependency'
function detect_dependency()
{
    local dep_name=${1}
    local deps_log_dir=${2}
    local enable_quiet=${3}
    local enable_dry_run=${4}

    export dep_log_path="${deps_log_dir}/${dep_name}.sh"

    if [ -z "${enable_dry_run}" ]; then
        enable_dry_run="no"
    fi

    if [ -z "${enable_quiet}" ]; then
        enable_quiet="no"
    fi

    ## Only proceed if a dependency directory is defined
    if [ ! -z "${deps_log_dir}" ]; then
        ## Check if a log file exists. If so, export its values for the caller.
        if [ -f "${dep_log_path}" ]; then
            ## Avoid other side-effects if dry-run is enabled
            if [ "${enable_dry_run}" = "no" ]; then
                . "${dep_log_path}"

                ## The following values are exported:
                #> export dep_name
                #> export src_dir
                #> export dep_ver
                #> export timestamp

                echo "DETECT [dep <- '${dep_name}', dir <- '${src_dir}']"

                if [ "${enable_quiet}" = "no" ]; then
                    tail --lines=5 "${dep_log_path}"
                fi
            fi

            return 0
        fi
    fi

    return 1
}


## Function for detecting possible conflicting system packages
## The first argument, 'pkg_list', is a space seperated list of packages to process
## The second argument, 'detect_mode', can be specified as follows:
export PKG_MODE_FOUND_FATAL="found:fatal"           ## If the package is detected, abort
export PKG_MODE_FOUND_NONFATAL="found:nonfatal"     ## If the package is detected, remove package
export PKG_MODE_MISSING_FATAL="missing:fatal"       ## If the package is missing,  abort
export PKG_MODE_MISSING_NONFATAL="missing:nonfatal" ## If the package is missing,  install package
## If 'detect_mode' is not specified, the fuction will return the result of the search
function detect_packages()
{
    local pkg_list=${1}
    local detect_mode=${2}

    dpkg --list --no-pager ${pkg_list}

    case "${detect_mode}" in
        "${PKG_MODE_FOUND_FATAL}")
            if [ "$?" -eq 0 ]; then
                ## Command successful => Package found
                local pkg_warn_msg="Detected conflicting installed system package(s) '${pkg_list}'."
                print_warning "${pkg_warn_msg}"
                exit 1
            fi
            ;;
        "${PKG_MODE_MISSING_FATAL}")
            if [ "$?" -ne 0 ]; then
                ## Command failed => Package missing
                local pkg_warn_msg="Detected missing system package(s) '${pkg_list}'."
                print_warning "${pkg_warn_msg}"
                exit 1
            fi
            ;;
        "${PKG_MODE_FOUND_NONFATAL}")
            if [ "$?" -eq 0 ]; then
                ## Command successful => Package found
                local pkg_msg="Removing conflicting system package(s) '${pkg_list}'."
                echo "${pkg_msg}"
                sudo apt-get remove -y "${pkg_list}"
            fi
            ;;
        "${PKG_MODE_MISSING_NONFATAL}")
            if [ "$?" -ne 0 ]; then
                ## Command failed => Package missing
                local pkg_msg="Installing missing system package(s) '${pkg_list}'."
                echo "${pkg_msg}"
                sudo apt-get install -y "${pkg_list}"
            fi
            ;;
        *)
            ;;
    esac

    return "$?"
}
