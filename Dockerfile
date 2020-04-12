FROM ubuntu:18.04

RUN apt-get update && apt-get install -y sudo locales apt-transport-https ca-certificates && rm -rf /var/lib/apt/lists/* \
    && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8

ENV LANG=en_US.utf8
ENV DEBIAN_FRONTEND=noninteractive 

RUN apt-get update && apt-get dist-upgrade -y && apt-get install -y curl gnupg
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN curl -q https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list
RUN echo "deb http://apt.llvm.org/eoan/ llvm-toolchain-eoan-10 main" > /etc/apt/sources.list.d/llvm.list
RUN echo "deb http://packages.ros.org/ros/ubuntu bionic main" > /etc/apt/sources.list.d/ros-latest.list
RUN apt-key adv --keyserver 'hkp://keyserver.ubuntu.com:80' --recv-key C1CF6E31E6BADE8868B172B4F42ED6FBAB17C654

RUN apt-get update && apt-get install -y \
	clang cmake libc++-dev libc++abi-dev bazel libopencv-dev libeigen3-dev libboost-dev libboost-thread-dev libboost-system-dev git \
	cmake libgoogle-glog-dev libatlas-base-dev libeigen3-dev libsuitesparse-dev libboost-dev libboost-filesystem-dev libopencv-dev libboost-filesystem-dev \
	python-catkin-tools \
	sudo libblas-dev libgoogle-glog-dev libatlas-base-dev libeigen3-dev libsuitesparse-dev libboost-dev libboost-filesystem-dev libopencv-dev libboost-filesystem-dev

RUN git clone --branch 3.4.6 https://github.com/opencv/opencv/ && \
	git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/ && \
	mkdir opencv/build/ && \
	cd opencv/build/ && \
	cmake -DOPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules .. && \
	make -j8 && \
	make install


ENV CC=clang CXX=clang++

WORKDIR /app

RUN apt install -y ros-melodic-desktop-full

RUN apt install -y rsync

# Configure docker user
ARG USER_ID
ARG USER_NAME
ARG GROUP_ID
ARG GROUP_NAME
RUN groupadd --gid "${GROUP_ID}" "${GROUP_NAME}" && \
	useradd --base-dir /home --gid "${GROUP_ID}" --groups sudo --create-home --uid "${USER_ID}" -o --shell /bin/bash "${USER_NAME}" && \
	passwd --delete "${USER_NAME}" && \
true
ENV HOME="/home/${USER_NAME}"
USER "${USER_NAME}"
