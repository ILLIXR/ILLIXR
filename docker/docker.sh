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
MAKE_ALL=0

# image names
WITH_ZED="with_zed"
WITH_LGPL="with_lgpl"
WITH_L_ZED="with_zed_lgpl"

BASE_IMAGE="illixr/base_image"

while getopts aglzh opt; do
    case $opt in
        a)
            MAKE_ALL=1
            ;;
        g)
            NO_GPL=1
            ;;
        h)
            cat <<EOF
$0 [-aghlz]

This script can be used to generate ILLIXR Docker images
  -a : make all possible images
  -g : docker image will have no GPL licensed code
  -l : docker image will have no LGPL or GPL licensed code, implies -g
  -z : docker image will not have the ZED SDK
EOF
            exit 1
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

echo "# --------------------------------------------------------------------------"
echo "# Building ILLIXR"
echo "#  ILLIXR_VERSION  :  ${ILLIXR_VERSION}"
echo "#  CUDA_VERSION    : ${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}"
echo "#  UBUNTU_RELEASE  : ${UBUNTU_RELEASE_YEAR}.04"
if [[ $NO_ZED -eq 0 ]]; then
    echo "#  ZED SDK         : ${ZED_SDK_MAJOR}.${ZED_SDK_MINOR}.${ZED_SDK_PATCH}"
fi

img_root="illixr_"
build_image=0
if [[ $MAKE_ALL -eq 1 ]]; then
    B_SUFFIX="s"
    IMGS="${img_root}full, ${img_root}no_ZED, ${img_root}no_GPL,\n#                    ${img_root}no_LGPL, ${img_root}no_GPL_ZED, ${img_root}no_LGPL_ZED"
else
    B_SUFFIX=""
    # shellcheck disable=SC2004
    if [[ $((${NO_LGPL} + ${NO_GPL} + ${NO_ZED})) -gt 0 ]]; then
        IMGS="${img_root}_no"
        if [[ $NO_LGPL -ne 0 ]]; then
            IMGS="${IMGS}_LGPL"
        elif [[ $NO_GPL -ne 0 ]]; then
            IMGS="${IMGS}_GPL"
        fi
        if [[ $NO_ZED -ne 0 ]]; then
            IMGS="${IMGS}_ZED"
        fi
    else
        IMGS="${img_root}full"
    fi
fi

echo -e "#  Building image${B_SUFFIX} : ${IMGS}"
echo "# --------------------------------------------------------------------------"

# build root image
docker build \
    --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
    --build-arg CUDA_MAJOR=${CUDA_MAJOR} \
    --build-arg CUDA_MINOR=${CUDA_MINOR} \
    --build-arg CUDA_PATCH=${CUDA_PATCH} \
    --build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR} \
    --tag ${BASE_IMAGE}:${ILLIXR_VERSION} \
    --file base/Dockerfile \
    .

BUILD_FLAGS=""

function build_image {
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg PARENT_IMAGE="$1" \
        --build-arg BUILD_FLAGS="$2" \
        --tag "illixr/illixr$3:v${ILLIXR_VERSION}" \
        --file build/Dockerfile \
        .
}

function build_zed {
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg CUDA_MAJOR=${CUDA_MAJOR} \
        --build-arg CUDA_MINOR=${CUDA_MINOR} \
        --build-arg CUDA_PATCH=${CUDA_PATCH} \
        --build-arg UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR} \
        --build-arg ZED_SDK_MAJOR=4 \
        --build-arg ZED_SDK_MINOR=2 \
        --build-arg ZED_SDK_PATCH=3 \
        --build-arg PARENT_IMAGE=illixr/"$1" \
        --tag illixr/"$2":${ILLIXR_VERSION} \
        --file zed/Dockerfile \
        .
}

function build_lgpl {
    docker build \
        --build-arg ILLIXR_VERSION=${ILLIXR_VERSION} \
        --build-arg PARENT_IMAGE="$1" \
        --tag illixr/"$2":${ILLIXR_VERSION} \
        --file lgpl/Dockerfile \
        .
}

if [[ $MAKE_ALL -eq 1 ]]; then
    build_image ${BASE_IMAGE} "-DNO_ZED=ON -DNO_GPL=ON -DNO_LGPL=ON" "_no_LGPL_ZED"
    build_zed ${BASE_IMAGE} ${WITH_ZED}
    build_lgpl ${BASE_IMAGE} ${WITH_LGPL}
    BASE_IMAGE="illixr/${WITH_ZED}"
    build_image ${BASE_IMAGE} "-DNO_LGPL=ON-DNO_GPL=ON" "_no_LGPL"
    BASE_IMAGE="illixr/${WITH_LGPL}"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON -DNO_GPL=ON" "_no_GPL_ZED"
    build_image ${BASE_IMAGE} "-DNO_ZED=ON" "_no_ZED"


    build_zed ${BASE_IMAGE} "${WITH_LGPL}_${WITH_ZED}"
    BASE_IMAGE="illixr/${WITH_LGPL}_${WITH_ZED}"
    build_image ${BASE_IMAGE} "" "full"
    build_image ${BASE_IMAGE} "-DNO_GPL=ON" "_no_GPL"

else
    TAG_TAIL=""
    if [[ $NO_ZED -eq 0 ]]; then
        build_zed ${BASE_IMAGE} ${WITH_ZED}
        BASE_IMAGE="illixr/${WITH_ZED}"
    else
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_ZED=ON"
        TAG_TAIL="_ZED"
    fi
    if [[ $NO_LGPL -eq 0 ]]; then
        build_lgpl ${BASE_IMAGE} ${WITH_LGPL}
        BASE_IMAGE="illixr/${WITH_LGPL}"
    elif [[ $NO_GPL -ne 0 ]]; then
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_GPL=ON"
        TAG_TAIL="_GPL${TAIL_TAG}"
    else
        BUILD_FLAGS="${BUILD_FLAGS} -DNO_LGPL=ON"
        TAG_TAIL="_LGPL${TAG_TAIL}"
    fi
    if [[ -z "${TAG_TAIL}" ]]; then
        TAG_TAIL="_full"
    else
        TAG_TAIL="_no${TAG_TAIL}"
    fi
    build_image ${BASE_IMAGE} "${BUILD_FLAGS}" "${TAG_TAIL}"
fi
