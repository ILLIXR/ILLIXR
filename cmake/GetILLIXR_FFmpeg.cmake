find_package(PkgConfig REQUIRED)

# add any custom module paths for finding cmake and pkg-config files
set(CMAKE_MODULE_PATH ${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/share/cmake;${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/lib64/cmake;${CMAKE_PREFIX_PATH})

set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig")

pkg_check_modules(libavcodec_illixr libavcodec_illixr)
pkg_check_modules(libavdevice_illixr libavdevice_illixr)
pkg_check_modules(libavformat_illixr libavformat_illixr)
pkg_check_modules(libavutil_illixr libavutil_illixr)
pkg_check_modules(libswscale_illixr libswscale_illixr)

set(FINALIZE_INSTALL "#!/usr/bin/env sh

rm -rf ${CMAKE_INSTALL_PREFIX}/include/libavcodec_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libavcodec ${CMAKE_INSTALL_PREFIX}/include/libavcodec_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libavdevice_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libavdevice ${CMAKE_INSTALL_PREFIX}/include/libavdevice_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libavfilter_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libavfilter ${CMAKE_INSTALL_PREFIX}/include/libavfilter_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libavformat_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libavformat ${CMAKE_INSTALL_PREFIX}/include/libavformat_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libavutil_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libavutil ${CMAKE_INSTALL_PREFIX}/include/libavutil_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libswresample_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libswresample ${CMAKE_INSTALL_PREFIX}/include/libswresample_illixr
rm -rf ${CMAKE_INSTALL_PREFIX}/include/libswscale_illixr/
mv ${CMAKE_INSTALL_PREFIX}/include/libswscale ${CMAKE_INSTALL_PREFIX}/include/libswscale_illixr
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libavcodec\\//\\\"libavcodec_illixr\\//g' {} \\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libavformat\\//\\\"libavformat_illixr\\//g' {} \\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libavdevice\\//\\\"libavdevice_illixr\\//g' {} \\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libavfilter\\//\\\"libavfilter_illixr\\//g' {} \\\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libavutil\\//\\\"libavutil_illixr\\//g' {} \\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libswresample\\//\\\"libswresample_illixr\\//g' {} \\\;
find ${CMAKE_INSTALL_PREFIX}/include -type f -exec sed -i 's/\\\"libswscale\\//\\\"libswscale_illixr\\//g' {} \\\;
")

FILE(WRITE ${CMAKE_BINARY_DIR}/finalize_install.sh ${FINALIZE_INSTALL})
FILE(CHMOD ${CMAKE_BINARY_DIR}/finalize_install.sh PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

if(NOT (libavcodec_illixr_FOUND AND libavdevice_illixr_FOUND AND
        libavformat_illixr_FOUND AND libavutil_illixr_FOUND AND libswscale_illixr_FOUND))
    message("FFMPEG NOT FOUND, will build from source")
    EXTERNALPROJECT_ADD(ILLIXR_FFmpeg_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/FFmpeg.git
                        GIT_TAG 654802ac3c22b50ecac4c4f6ef8e1847f54e6364
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/ffmpeg
                        DEPENDS ${Vulkan_DEP_STR}
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DEPENDENCY_COMPILE_ARGS}
                        BUILD_COMMAND cmake --build . --parallel ${BUILD_PARALLEL_LEVEL}
                        INSTALL_COMMAND ${CMAKE_BINARY_DIR}/finalize_install.sh)
    set(FFMPLIBS avcodec;avdevice;avformat;avutil;swscale)
    foreach(LIBRARY IN LISTS FFMPLIBS)
        set(lib${LIBRARY}_illixr_CFLAGS "-I${CMAKE_INSTALL_PREFIX}/include" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_CFLAGS_I "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_CFLAGS_OTHER "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_FOUND "1" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-l${LIBRARY}_illixr" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LDFLAGS_OTHER "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBRARIES "${LIBRARY}_illixr" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBS "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBS_L "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBS_OTHER "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBS_PATHS "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LINK_LIBRARIES "${CMAKE_INSTALL_PREFIX}/lib/lib${LIBRARY}_illixr.so" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_MODULE_NAME "lib${LIBRARY}_illixr" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_PREFIX ${CMAKE_INSTALL_PREFIX} CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_CFLAGS "-I${CMAKE_INSTALL_PREFIX}/include" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_CFLAGS_I "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_CFLAGS_OTHER "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_LIBDIR "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_LIBS "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_LIBS_L "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_LIBS_OTHER "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_STATIC_LIBS_PATHS "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_lib${LIBRARY}_illixr_INCLUDEDIR "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_lib${LIBRARY}_illixr_LIBDIR "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_lib${LIBRARY}_illixr_PREFIX "" CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_lib${LIBRARY}_illixr_VERSION "" CACHE INTERNAL "" FORCE)
    endforeach()

    set(libavcodec_illixr_STATIC_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11" CACHE INTERNAL "" FORCE)
    set(libavcodec_illixr_STATIC_LDFLAGS_OTHER "-pthread;-pthread;-pthread" CACHE INTERNAL "" FORCE)
    set(libavcodec_illixr_STATIC_LIBRARIES "avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11" CACHE INTERNAL "" FORCE)
    set(libavcodec_illixr_STATIC_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib" CACHE INTERNAL "" FORCE)
    set(libavcodec_illixr_VERSION "60.37.100" CACHE INTERNAL "" FORCE)



    set(libavdevice_illixr_STATIC_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-lavdevice_illixr;-lm;-latomic;-ldrm;-lxcb;-lxcb-shm;-lxcb-shape;-lxcb-xfixes;-lasound;-lGL;-lpulse;-pthread;-lSDL2;-lsndio;-lv4l2;-lXv;-lX11;-lXext;-L${CMAKE_INSTALL_PREFIX}/lib;-lavfilter_illixr;-pthread;-lm;-latomic;-lbs2b;-lass;-lzimg;-lOpenCL;-lfontconfig;-lfreetype;-lfreetype;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lm;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lssh;-L${CMAKE_INSTALL_PREFIX}/lib;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswscale_illixr;-lm;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lpostproc_illixr;-lm;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lssh;-L${CMAKE_INSTALL_PREFIX}/lib;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11" CACHE INTERNAL "" FORCE)
    set(libavdevice_illixr_STATIC_LDFLAGS_OTHER "-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread;-pthread" CACHE INTERNAL "" FORCE)
    set(libavdevice_illixr_STATIC_LIBRARIES "avdevice_illixr;m;atomic;drm;xcb;xcb-shm;xcb-shape;xcb-xfixes;asound;GL;pulse;SDL2;sndio;v4l2;Xv;X11;Xext;avfilter_illixr;m;atomic;bs2b;ass;zimg;OpenCL;fontconfig;freetype;freetype;m;atomic;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;m;atomic;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;ssh;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swscale_illixr;m;atomic;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;postproc_illixr;m;atomic;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;ssh;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11" CACHE INTERNAL "" FORCE)
    set(libavdevice_illixr_STATIC_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib" CACHE INTERNAL "" FORCE)
    set(libavdevice_illixr_VERSION "60.4.100" CACHE INTERNAL "" FORCE)

    set(libavformat_illixr_STATIC_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lssh;-L${CMAKE_INSTALL_PREFIX}/lib;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-pthread;-lm;-latomic;-llzma;-laom;-lmp3lame;-lm;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lz;-L${CMAKE_INSTALL_PREFIX}/lib;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lswresample_illixr;-lm;-lsoxr;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11" CACHE INTERNAL "" FORCE)
    set(libavformat_illixr_STATIC_LDFLAGS_OTHER "-pthread;-pthread;-pthread;-pthread;-pthread" CACHE INTERNAL "" FORCE)
    set(libavformat_illixr_STATIC_LIBRARIES "avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;ssh;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;m;atomic;lzma;aom;mp3lame;m;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;z;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11;swresample_illixr;m;soxr;atomic;vdpau;X11;m;drm;OpenCL;atomic;X11;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11" CACHE INTERNAL "" FORCE)
    set(libavformat_illixr_STATIC_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/lib" CACHE INTERNAL "" FORCE)
    set(libavformat_illixr_VERSION "60.20.100" CACHE INTERNAL "" FORCE)


    set(libavutil_illixr_STATIC_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11" CACHE INTERNAL "" FORCE)
    set(libavutil_illixr_STATIC_LDFLAGS_OTHER "-pthread" CACHE INTERNAL "" FORCE)
    set(libavutil_illixr_STATIC_LIBRARIES "avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11" CACHE INTERNAL "" FORCE)
    set(libavutil_illixr_STATIC_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/lib" CACHE INTERNAL "" FORCE)
    set(libavutil_illixr_VERSION "58.36.101" CACHE INTERNAL "" FORCE)

    set(libswscale_illixr_STATIC_LDFLAGS "-L${CMAKE_INSTALL_PREFIX}/lib;-lswscale_illixr;-lm;-latomic;-L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lOpenCL;-latomic;-lX11" CACHE INTERNAL "" FORCE)
    set(libswscale_illixr_STATIC_LDFLAGS_OTHER "-pthread" CACHE INTERNAL "" FORCE)
    set(libswscale_illixr_STATIC_LIBRARIES "swscale_illixr;m;atomic;avutil_illixr;vdpau;X11;m;drm;OpenCL;atomic;X11" CACHE INTERNAL "" FORCE)
    set(libswscale_illixr_STATIC_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/lib" CACHE INTERNAL "" FORCE)
    set(libswscale_illixr_VERSION "7.6.100" CACHE INTERNAL "" FORCE)

    set(ILLIXR_FFmpeg_EXTERNAL Yes)
    set(ILLIXR_FFmpeg_DEP_STR ILLIXR_FFmpeg_ext)
else()
    message("FFMPEG FOUND")
endif()
