FROM ubuntu:eoan

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get dist-upgrade -y && apt-get install -y curl gnupg
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN curl -q https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list
RUN echo "deb http://apt.llvm.org/eoan/ llvm-toolchain-eoan-10 main" > /etc/apt/sources.list.d/llvm.list

RUN apt-get update && apt-get install -y \
    clang cmake libc++-dev libc++abi-dev bazel libopencv-dev libeigen3-dev libboost-dev libboost-thread-dev libboost-system-dev git

ENV CC=clang CXX=clang++

WORKDIR /app

RUN apt-get install -y sudo libblas-dev
