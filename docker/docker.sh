#!/bin/bash

set -o pipefail
set -e

ROOT_DIR=$(dirname "$0")
cd "${ROOT_DIR}"

ILLIXR_VERSION=$(cat ../VERSION)
CUDA_MAJOR=12
CUDA_MINOR=6
CUDA_PATCH=3

UBUNTU_RELEASE_YEAR=24

ZED_SDK_MAJOR=4
ZED_SDK_MINOR=2
ZED_SDK_PATCH=3

NO_GPL=0
NO_LGPL=0
NO_ZED=0
BUILD_FULL=0
MAKE_ALL=0
USE_LOG=0
PARALLEL=4

# image names
WITH_ZED="with_zed"
WITH_GPL="with_gpl"
WITH_GPL_ZED="with_zed_gpl"
WITH_LGPL="with_lgpl"

BASE_IMAGE="illixr/base_image"

function print_help {
    cat <<EOF

$0 [-afghlopr:z]

This script can be used to generate ILLIXR Docker images
  -a           : make all possible images (exclusive with all other flags)
  -f           : docker image will have everything (exclusive with all other flags)
  -g           : docker image will have no GPL licensed code (exclusive with -a and -f)
  -h           : print this help and exit
  -l           : docker image will have no LGPL or GPL licensed code, implies -g (exclusive with -a and -f)
  -o           : save docker build output to log files
  -p           : parallel level for build (default is 4)
  -r <version> : the version for the images, defaults to value in VERSION file
  -z           : docker image will not have the ZED SDK (exclusive with -a and -f)
EOF
}

while getopts aglzhfop:r: opt; do
    case $opt in
        a)
            MAKE_ALL=1
            ;;
        f)
            BUILD_FULL=1
            ;;
        g)
            NO_GPL=1
            ;;
        h)
            print_help
            exit 0
            ;;
        l)
            NO_GPL=1
            NO_LGPL=1
            ;;
        o)
            USE_LOG=1
            ;;
        p)
            PARALLEL=$OPTARG
            ;;
        r)
            ILLIXR_VERSION=$OPTARG
            ;;
        z)
            NO_ZED=1
            ;;
        *)
            echo "Invalid argument $opt"
            ;;
    esac
done

if [[ $((NO_LGPL + NO_GPL + NO_ZED + BUILD_FULL + MAKE_ALL)) -eq 0 ]]; then
    echo "No build flags given, at least 1 is required."
    print_help
    exit 1
fi

if [[ $BUILD_FULL -eq 1 ]]; then
    if [[ $((NO_LGPL + NO_GPL + NO_ZED + MAKE_ALL)) -gt 0 ]]; then
        echo "Invalid flag combination $*"
        print_help
        exit 1
    fi
fi

if [[ $MAKE_ALL -eq 1 ]]; then
    if [[ $((NO_LGPL + NO_GPL + NO_ZED)) -gt 0 ]]; then
        echo "Invalid flag combination $*"
        print_help
        exit 1
    fi
fi


echo "# --------------------------------------------------------------------------"
echo "# Building ILLIXR"
echo "#  ILLIXR_VERSION  :  ${ILLIXR_VERSION}"
echo "#  CUDA_VERSION    : ${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}"
echo "#  UBUNTU_RELEASE  : ${UBUNTU_RELEASE_YEAR}.04"
if [[ $NO_ZED -eq 0 ]]; then
    echo "#  ZED SDK         : ${ZED_SDK_MAJOR}.${ZED_SDK_MINOR}.${ZED_SDK_PATCH}"
fi
if [[ USE_LOG -ne 0 ]]; then
    echo "# Capturing output : Yes"
    DOC_FLAGS="--progress=plain"
else
    echo "# Capturing output : No"
    DOC_FLAGS=""
fi

img_root="illixr_"

if [[ $MAKE_ALL -eq 1 ]]; then
    B_SUFFIX="s"
    IMAGES="${img_root}full, ${img_root}no_zed, ${img_root}no_gpl,\n#                    ${img_root}no_lgpl, ${img_root}no_gpl_zed, ${img_root}no_lgpl_zed"
else
    B_SUFFIX=""
    # shellcheck disable=SC2004
    if [[ $((NO_LGPL + NO_GPL + NO_ZED)) -gt 0 ]]; then
        IMAGES="${img_root}_no"
        if [[ $NO_LGPL -ne 0 ]]; then
            IMAGES="${IMAGES}_lgpl"
        elif [[ $NO_GPL -ne 0 ]]; then
            IMAGES="${IMAGES}_gpl"
        fi
        if [[ $NO_ZED -ne 0 ]]; then
            IMAGES="${IMAGES}_zed"
        fi
    else
        IMAGES="${img_root}full"
    fi
fi

echo -e "#  Building image${B_SUFFIX} : ${IMAGES}"
echo "# --------------------------------------------------------------------------"

# build root image
echo ""
echo "Building base image"
LOG=""
command=("docker build " \
    "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
    "--build-arg CUDA_MAJOR=${CUDA_MAJOR}" \
    "--build-arg CUDA_MINOR=${CUDA_MINOR}" \
    "--build-arg CUDA_PATCH=${CUDA_PATCH}" \
    "--build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR}" \
    "--tag ${BASE_IMAGE}:${ILLIXR_VERSION}" \
    "--file base/Dockerfile" \
    "${DOC_FLAGS}" \
    ".")

if [[ USE_LOG -ne 0 ]]; then
    LOG="2>&1 | tee -a base.log"
    echo "${command[@]}" > base.log
fi

eval "${command[@]}" "${LOG}"



echo ""
echo "Base image complete"
BUILD_FLAGS=""

function build_image {
    echo""
    echo "Building final image illixr$3 from $1 with flags $2"
    command=("docker build" \
        "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
        "--build-arg PARENT_IMAGE=$1" \
        "--build-arg BUILD_FLAGS=\"$2\"" \
        "--build-arg PARALLEL=${PARALLEL}" \
        "--tag illixr/illixr$3:${ILLIXR_VERSION}" \
        "--file build/Dockerfile" \
        "${DOC_FLAGS}" \
        ".")
    if [[ USE_LOG -ne 0 ]]; then
        LOG="2>&1 | tee -a $3.log"
        echo "${command[@]}" > "$3.log"
    fi

    eval "${command[@]}" "${LOG}"
}

function incremental_build_image {
    echo""
    echo "Building final image illixr$3 from $1 with flags $2"
    command=("docker build" \
        "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
        "--build-arg PARENT_IMAGE=$1" \
        "--build-arg BUILD_FLAGS=\"$2\"" \
        "--build-arg PARALLEL=${PARALLEL}" \
        "--tag illixr/illixr$3:${ILLIXR_VERSION}" \
        "--file incremental/Dockerfile" \
        "${DOC_FLAGS}" \
        ".")
    if [[ USE_LOG -ne 0 ]]; then
        LOG="2>&1 | tee -a $3.log"
        echo "${command[@]}" > "$3.log"
    fi

    eval "${command[@]}" "${LOG}"
}

function build_zed {
    echo ""
    echo "Building ZED image $2 from $1"
    command=("docker build" \
        "--progress plain" \
        "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
        "--build-arg CUDA_MAJOR=${CUDA_MAJOR}" \
        "--build-arg CUDA_MINOR=${CUDA_MINOR}" \
        "--build-arg CUDA_PATCH=${CUDA_PATCH}" \
        "--build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR}" \
        "--build-arg ZED_SDK_MAJOR=4" \
        "--build-arg ZED_SDK_MINOR=2" \
        "--build-arg ZED_SDK_PATCH=3" \
        "--build-arg PARENT_IMAGE=$1" \
        "--tag illixr/$2:${ILLIXR_VERSION}" \
        "--file zed/Dockerfile" \
        "${DOC_FLAGS}" \
        ".")

    if [[ USE_LOG -ne 0 ]]; then
        LOG="2>&1 | tee -a $2.log"
        echo "${command[@]}" > "$2.log"
    fi

    eval "${command[@]}" "${LOG}"

    echo ""
    echo "ZED build complete"
}

function build_lgpl {
    echo "Building LGPL image $2 from $1"
    command=("docker build" \
        "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
        "--build-arg PARENT_IMAGE=$1" \
        "--tag illixr/$2:${ILLIXR_VERSION}" \
        "--file lgpl/Dockerfile" \
        "${DOC_FLAGS}" \
        ".")

    if [[ USE_LOG -ne 0 ]]; then
        LOG="2>&1 | tee -a $2.log"
        echo "${command[@]}" > "$2.log"
    fi

    eval "${command[@]}" "${LOG}"

    echo ""
    echo "LGPL build complete"
}

function build_gpl {
    echo "Building GPL image $2 from $1"
    command=("docker build" \
        "--build-arg ILLIXR_VERSION=${ILLIXR_VERSION}" \
        "--build-arg CUDA_MAJOR=${CUDA_MAJOR}" \
        "--build-arg CUDA_MINOR=${CUDA_MINOR}" \
        "--build-arg PARENT_IMAGE=$1" \
        "--tag illixr/$2:${ILLIXR_VERSION}" \
        "--file gpl/Dockerfile" \
        "${DOC_FLAGS}" \
        ".")

    if [[ USE_LOG -ne 0 ]]; then
        LOG="2>&1 | tee -a $2.log"
        echo "${command[@]}" > "$2.log"
    fi

    eval "${command[@]}" "${LOG}"

    echo ""
    echo "GPL build complete"
}

if [[ $MAKE_ALL -eq 1 ]]; then
    echo "----------------------------"
    echo " BUILDING no-lgpl-zed"
    echo "----------------------------"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON -DNO_GPL=ON -DNO_LGPL=ON" "_no_lgpl_zed"

    BASE_IMAGE="illixr/illixr_no_lgpl_zed"
    echo "----------------------------"
    echo " BUILDING no-gpl-zed"
    echo "----------------------------"
    build_lgpl ${BASE_IMAGE} ${WITH_LGPL}
    incremental_build_image "illixr/${WITH_LGPL}" "-DNO_ZED=ON -DNO_GPL=ON -DNO_LGPL=OFF" "_no_gpl_zed"

    echo "----------------------------"
    echo " BUILDING no-zed"
    echo "----------------------------"
    build_gpl "illixr/illixr_no_gpl_zed" ${WITH_GPL}
    incremental_build_image "illixr/${WITH_GPL}" "-DNO_ZED=ON -DNO_GPL=OFF -DNO_LGPL=OFF" "_no_zed"


    echo "----------------------------"
    echo " BUILDING no-lgpl"
    echo "----------------------------"
    build_zed ${BASE_IMAGE} ${WITH_ZED}
    incremental_build_image "illixr/${WITH_ZED}" "-DNO_ZED=OFF -DNO_LGPL=ON -DNO_GPL=ON" "_no_lgpl"

    echo "----------------------------"
    echo " BUILDING no-gpl"
    echo "----------------------------"
    build_lgpl "illixr/illixr_no_lgpl" ${WITH_LGPL}${WITH_ZED}
    incremental_build_image "illixr/${WITH_LGPL}${WITH_ZED}" "-DNO_ZED=OFF -DNO_GPL=ON -DNO_LGPL=OFF" "_no_gpl"


    echo "----------------------------"
    echo " BUILDING full"
    echo "----------------------------"
    build_gpl "illixr/illixr_no_gpl" ${WITH_GPL}${WITH_ZED}
    incremental_build_image "illixr/${WITH_GPL}${WITH_ZED}" "-DNO_ZED=OFF -DNO_GPL=OFF -DNO_LGPL=OFF" "full"

else
    TAG_TAIL=""
    if [[ $NO_ZED -eq 0 ]]; then
        build_zed ${BASE_IMAGE} ${WITH_ZED}
        BASE_IMAGE="illixr/${WITH_ZED}"
    else
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_ZED=ON"
        TAG_TAIL="_zed"
    fi
    if [[ $NO_LGPL -eq 0 ]]; then
        build_lgpl ${BASE_IMAGE} ${WITH_LGPL}
        BASE_IMAGE="illixr/${WITH_LGPL}"
    else
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_LGPL=ON"
        TAG_TAIL="_lgpl${TAG_TAIL}"
    fi
    if [[ $NO_GPL -eq 0 ]]; then
        build_gpl ${BASE_IMAGE} ${WITH_GPL}
        BASE_IMAGE="illixr/${WITH_GPL}"
    else
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_GPL=ON"
        if [[ $NO_LGPL -ne 0 ]]; then
            TAG_TAIL="_gpl${TAIL_TAG}"
        fi
    fi
    if [[ -z "${TAG_TAIL}" ]]; then
        TAG_TAIL="_full"
    else
        TAG_TAIL="_no${TAG_TAIL}"
    fi

    build_image ${BASE_IMAGE} "${BUILD_FLAGS}" "${TAG_TAIL}"
fi
