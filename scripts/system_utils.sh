#!/bin/bash

#--- Script defining bash helper functions ---#


## Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }


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

    dpkg -l ${pkg_list}

    case "${detect_mode}" in
        "${PKG_MODE_FOUND_FATAL}")
            if [ "$?" == 0 ]; then
                ## Command successful => Package found
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
        "${PKG_MODE_FOUND_NONFATAL}")
            if [ "$?" == 0 ]; then
                ## Command successful => Package found
                local pkg_msg="Removing conflicting system package(s) '${pkg_list}'."
                echo "${pkg_msg}"
                sudo apt-get remove -y "${pkg_list}"
            fi
            ;;
        "${PKG_MODE_MISSING_NONFATAL}")
            if [ "$?" != 0 ]; then
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
