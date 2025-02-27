#!/bin/bash

set -o pipefail
set -e

cd $(dirname $0)

ILLIXR_VERSION=3.3
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

# image names
WITH_ZED="with_zed"
WITH_GPL="with_gpl"
WITH_GPL_ZED="with_zed_gpl"
WITH_LGPL="with_lgpl"

BASE_IMAGE="illixr/base_image"

function print_help {
    cat <<EOF

$0 [-afghlz]

This script can be used to generate ILLIXR Docker images
  -a : make all possible images (exclusive with all other flags)
  -f : docker image will have everything (exclusive with all other flags)
  -g : docker image will have no GPL licensed code (exclusive with -a and -f)
  -h : print this help and exit
  -l : docker image will have no LGPL or GPL licensed code, implies -g (exclusive with -a and -f)
  -z : docker image will not have the ZED SDK (exclusive with -a and -f)
EOF
}

while getopts aglzhf opt; do
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
docker build \
    --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
    --build-arg CUDA_MAJOR=${CUDA_MAJOR} \
    --build-arg CUDA_MINOR=${CUDA_MINOR} \
    --build-arg CUDA_PATCH=${CUDA_PATCH} \
    --build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR} \
    --tag ${BASE_IMAGE}:${ILLIXR_VERSION} \
    --file base/Dockerfile \
    .
echo ""
echo "Base image complete"
BUILD_FLAGS=""

function build_image {
    echo""
    echo "Building final image illixr_$3 from $1 with flags $2"
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg PARENT_IMAGE="$1" \
        --build-arg BUILD_FLAGS="$2" \
        --tag "illixr/illixr$3:v${ILLIXR_VERSION}" \
        --file build/Dockerfile \
        .
}

function build_zed {
    echo ""
    echo "Building ZED image $2 from $1"
    docker build \
        --progress plain \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg CUDA_MAJOR=${CUDA_MAJOR} \
        --build-arg CUDA_MINOR=${CUDA_MINOR} \
        --build-arg CUDA_PATCH=${CUDA_PATCH} \
        --build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR} \
        --build-arg ZED_SDK_MAJOR=4 \
        --build-arg ZED_SDK_MINOR=2 \
        --build-arg ZED_SDK_PATCH=3 \
        --build-arg PARENT_IMAGE="$1" \
        --tag "illixr/$2:${ILLIXR_VERSION}" \
        --file zed/Dockerfile \
        .
    echo ""
    echo "ZED build complete"
}

function build_lgpl {
    echo "Building LGPL image $2 from $1"
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg PARENT_IMAGE="$1" \
        --tag "illixr/$2:${ILLIXR_VERSION}" \
        --file lgpl/Dockerfile \
        .
    echo ""
    echo "LGPL build complete"
}

function build_gpl {
    echo "Building GPL image $2 from $1"
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg CUDA_MAJOR=${CUDA_MAJOR} \
        --build-arg CUDA_MINOR=${CUDA_MINOR} \
        --build-arg PARENT_IMAGE="$1" \
        --tag "illixr/$2:${ILLIXR_VERSION}" \
        --file gpl/Dockerfile \
        .
    echo ""
    echo "GPL build complete"
}

if [[ $MAKE_ALL -eq 1 ]]; then
    printf "----------------------------\n BUILDING no-lgpl-zed\n----------------------------\n"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON -DNO_GPL=ON -DNO_LGPL=ON" "_no_lgpl_zed"
    build_zed ${BASE_IMAGE} ${WITH_ZED}
    build_gpl ${BASE_IMAGE} ${WITH_GPL}
    BASE_IMAGE="illixr/${WITH_ZED}"
    printf "----------------------------\n BUILDING no-lgpl\n----------------------------\n"
    build_image ${BASE_IMAGE} "-DNO_LGPL=ON-DNO_GPL=ON" "_no_lgpl"
    BASE_IMAGE="illixr/${WITH_GPL}"
    printf "----------------------------\n BUILDING no-gpl-zed\n----------------------------\n"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON -DNO_GPL=ON" "_no_gpl_zed"
    printf "----------------------------\n BUILDING no-zed\n----------------------------\n"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON" "_no_zed"

    build_zed ${BASE_IMAGE} "${WITH_GPL_ZED}"
    BASE_IMAGE="illixr/${WITH_GPL_ZED}"
    printf "----------------------------\n BUILDING full\n----------------------------\n"
    build_image ${BASE_IMAGE} "" "full"
    printf "----------------------------\n BUILDING no-gpl\n----------------------------\n"
    build_image ${BASE_IMAGE} "-DNO_GPL=ON" "_no_gpl"

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
