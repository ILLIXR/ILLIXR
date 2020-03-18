#!/bin/bash
set -o noclobber -o errexit -o nounset -o xtrace

# cd to the root of the project
cd "$(dirname "${0}")"

clean=true
extra_flags="--compilation_mode dbg"
# extra_flags="--compilation_mode fastbuild"

cd slam1
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} slam1
cd ..

cd cam1
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} cam1
cd ..

cd timewarp_gl
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} timewarp_gl
cd ..

cd gldemo
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} gldemo
cd ..

cd pose_prediction
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} pose_prediction
cd ..

cd imu1
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} imu1
cd ..

cd runtime
[ -n "${clean}" ] && bazel clean
bazel build ${extra_flags} main
cd ..

# I opted not to put this in one bazel package because in production,
# these packages do not know about each other. The user builds them
# separately or downloads binaries from the devs. All the user needs
# is all each .so files and the runtime binary.

./runtime/bazel-bin/main \
	cam1/bazel-bin/libcam1.so \
	imu1/bazel-bin/libimu1.so \
	slam1/bazel-bin/libslam1.so \
	pose_prediction/bazel-bin/libpose_prediction.so \
	timewarp_gl/bazel-bin/libtimewarp_gl.so \
	gldemo/bazel-bin/libgldemo.so \
;
