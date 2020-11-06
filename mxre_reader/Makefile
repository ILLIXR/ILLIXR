LDFLAGS = $(shell pkg-config --libs mxre glfw3 glew)
#CFLAGS = -D__DEBUG__ -D__USE_OPENCV_CUDA__ -D__PROFILE__ $(shell pkg-config --cflags mxre)
CFLAGS = -D__DEBUG__ -D__USE_OPENCV_CUDA__ -D__PROFILE__ -DL1D_CACHE_LINE_SIZE=64 -pthread -DL1D_CACHE_LINE_SIZE=64 -I/usr/local/include -I/usr/local/include/mxreinc -I/home/jin/github/libs/FFmpeg/ffmpeg_build/include -I/usr/local/include -I/home/jin/github/libs/FFmpeg/ffmpeg_build/include -I/usr/local/include -I/usr/local/include/opencv -I/usr/local/include -I/usr/include/libdrm

include common/common.mk

