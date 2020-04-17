#!/bin/bash
{

set -o noclobber -o errexit -o nounset -o xtrace

# cd to the root of the project
cd "$(dirname "${0}")"

CXX=${CXX-clang++}
clean=true
extra_flags=

# Necessary for Open VINS standalone build
cd slam2/open_vins/ov_standalone/ov_msckf/
rm -rf build/
mkdir build/
cd build/
cmake ..
make -j`(nproc)`
cd ../../../../..

cd offline_imu_cam
"${CXX}" -g offline_imu_cam.cc -std=c++2a -pthread -lboost_thread `pkg-config --cflags --libs opencv4` `pkg-config opencv --cflags --libs` -shared -o liboffline_imu_cam.so -fpic
cd ..

cd runtime
"${CXX}" -g main.cc -std=c++2a -lglfw -lrt -lm -ldl -lGLEW -lGLU -lm -lGL -lpthread -pthread -lm -ldl -lX11-xcb -lxcb-glx -ldrm -lXdamage -lXfixes -lxcb-dri2 -lXxf86vm -lXext -lX11 -lpthread -lxcb -lXau -lXdmcp -pthread -lboost_thread -ldl `pkg-config --cflags --libs opencv4` `pkg-config opencv --cflags --libs` -o main.exe
"${CXX}" -g main.cc -std=c++2a -lglfw -lrt -lm -ldl -lGLEW -lGLU -lm -lGL -lpthread -pthread -lm -ldl -lX11-xcb -lxcb-glx -ldrm -lXdamage -lXfixes -lxcb-dri2 -lXxf86vm -lXext -lX11 -lpthread -lxcb -lXau -lXdmcp -pthread -lboost_thread -ldl `pkg-config --cflags --libs opencv4` `pkg-config opencv --cflags --libs` -fPIC -shared -o illixrrt.so
cd ..

cd timewarp_gl
#[ -n "${clean}" ] && bazel clean
#bazel build ${extra_flags} timewarp_gl
"${CXX}" -g utils/*.cpp timewarp_gl.cc --std=c++2a -lglfw -lrt -lm -ldl -lGLEW -lGLU -lm -lGL -lpthread -pthread -lm -ldl -lX11-xcb -lxcb-glx -ldrm -lXdamage -lXfixes -lxcb-dri2 -lXxf86vm -lXext -lX11 -lpthread -lxcb -lXau -lXdmcp  -shared  -o libtimewarp_gl.so -fpic
cd ..

cd hologram
make clean && make
cd ..

cd gldemo
#[ -n "${clean}" ] && bazel clean
#bazel build ${extra_flags} gldemo
#"${CXX}" -g utils/*.cpp gldemo.cc --std=c++2a -lglfw -lrt -lm -ldl -lGLEW -lGLU -lm -lGL -lpthread -pthread -lm -ldl -lX11-xcb -lxcb-glx -ldrm -lXdamage -lXfixes -lxcb-dri2 -lXxf86vm -lXext -lX11 -lpthread -lxcb -lXau -lXdmcp -shared -o libgldemo.so -fpic
cd ..

cd pose_prediction
#[ -n "${clean}" ] && bazel clean
#bazel build ${extra_flags} pose_prediction
"${CXX}" -g pose_prediction.cc kalman.cc --std=c++2a -I/usr/include/eigen3 -shared -o libpose_prediction.so -fpic
cd ..

if [ ! -e "data" ]
then
	if [ ! -e "data.zip" ]
	then
		curl -o data.zip \
			"http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_01_easy/V1_01_easy.zip"
		unzip data.zip
	fi
	mv mav0 data
	rm -rf __MACOSX
fi

# I opted not to put this in one bazel package because in production,
# these packages do not know about each other. The user builds them
# separately or downloads binaries from the devs. All the user needs
# is all each .so files and the runtime binary.

./runtime/main.exe \
	slam2/open_vins/ov_standalone/ov_msckf/build/libslam2.so \
	offline_imu_cam/liboffline_imu_cam.so \
	pose_prediction/libpose_prediction.so \
	timewarp_gl/libtimewarp_gl.so \
	gldemo/libgldemo.so \
	hologram/hologram.so \
;
	# cam1/bazel-bin/libcam1.so \
	# imu1/bazel-bin/libimu1.so \
	# slam1/bazel-bin/libslam1.so \



exit;
}
