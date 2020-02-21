#!/bin/sh
set -o noclobber -o errexit -o nounset -o xtrace

# cd to the root of the project
cd "$(dirname "${0}")"

clean=true

cd slam1
[ -n "${clean}" ] && bazel clean
bazel build slam1
cd ..

cd slam2
[ -n "${clean}" ] && bazel clean
bazel build slam2
cd ..

cd cam1
[ -n "${clean}" ] && bazel clean
bazel build cam1
cd ..

cd imu1
[ -n "${clean}" ] && bazel clean
bazel build imu1
cd ..

cd timewarp_gl
[ -n "${clean}" ] && bazel clean
bazel build timewarp_gl
cd ..

cd runtime
[ -n "${clean}" ] && bazel clean
bazel build main
cd ..

# I opted not to put this in one bazel package because in production,
# these packages do not know about each other. The user builds them
# separately or downloads binaries from the devs. All the user needs
# is all three .so files and the runtime binary.

./runtime/bazel-bin/main \
	slam1/bazel-bin/libslam1.so \
	cam1/bazel-bin/libcam1.so \
	imu1/bazel-bin/libimu1.so \
	slam2/bazel-bin/libslam2.so \
	timewarp_gl/bazel-bin/libtimewarp_gl.so
