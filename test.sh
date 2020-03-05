#!/bin/bash
set -o noclobber -o errexit -o nounset -o xtrace

# cd to the root of the project
cd "$(dirname "${0}")"


CXX=${CXX-clang++}

cd slam1
if [ ! -e "okvis/build" ]
then
	if [ ! -e "okvis" ]
	then
		git submodule update --init okvis
	fi
	cd okvis && ./build.sh  && cd ..
fi
"${CXX}" slam1.cc okvis/install/lib/*.a -Iokvis/install/include -std=c++2a -I /usr/include/eigen3/ -lpthread -shared -o libslam1.so -fpic
cd ..

cd offline_imu_cam
"${CXX}" offline_imu_cam.cc -std=c++2a -pthread `pkg-config --libs --cflags opencv` -shared -o liboffline_imu_cam.so -fpic
cd ..

cd runtime
"${CXX}" main.cc -std=c++2a -pthread -ldl -o main.exe
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
	slam1/libslam1.so \
	liboffline_imu_cam.so \
;
