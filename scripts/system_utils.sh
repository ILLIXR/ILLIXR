#!/bin/bash

#--- Script defining bash helper functions ---#


## Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }


## Function for detecting possible conflicting system packages
## The second argument, 'detect_mode', can be specified as follows:
export PKG_MODE_FOUND_FATAL="found:fatal"           ## If the package is detected, abort
export PKG_MODE_FOUND_NONFATAL="found:nonfatal"     ## If the package is detected, return
export PKG_MODE_MISSING_FATAL="missing:fatal"       ## If the package is missing, abort
export PKG_MODE_MISSING_NONFATAL="missing:nonfatal" ## If the package is missing, return
function detect_packages()
{
    local pkg_list=${1}
    local detect_mode=${2}

    dpkg -l ${pkg_list}

    case "${detect_mode}" in
        "${PKG_MODE_FOUND_FATAL}")
            if [ "$?" == 0 ]; then
                ## Command was successful => Package found
                local pkg_warn_msg="Detected conflicting installed system package(s) '${pkg_list}'."
                print_warning "${pkg_warn_msg}"
                exit 1
            fi
            ;;
        "${PKG_MODE_MISSING_FATAL}")
            if [ "$?" != 0 ]; then
                ## Command failed => Package missing
                local pkg_warn_msg="Detected missing system package(s) '${pkg_list}'."
                print_warning "${pkg_warn_msg}"
                exit 1
            fi
            ;;
        "${PKG_MODE_FOUND_NONFATAL}" | "${PKG_MODE_MISSING_NONFATAL}" | *)
            ;;
    esac

    return "$?"
}
