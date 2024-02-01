find_package(PkgConfig REQUIRED)

# add any custom module paths for finding cmake and pkg-config files
set(CMAKE_MODULE_PATH ${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/share/cmake;${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/lib64/cmake;${CMAKE_PREFIX_PATH})

set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/share/pkgconfig")

get_external(Vulkan)

pkg_check_modules(libavcodec_illixr QUIET libavcodec_illixr)
pkg_check_modules(libavdevice_illixr QUIET libavdevice_illixr)
pkg_check_modules(libavformat_illixr QUIET libavformat_illixr)
pkg_check_modules(libavutil_illixr QUIET libavutil_illixr)
pkg_check_modules(libswscale_illixr QUIET libswscale_illixr)

set(FINALIZE_INSTALL "#!/usr/bin/env sh

mv ${CMAKE_INSTALL_PREFIX}/include/libavcodec ${CMAKE_INSTALL_PREFIX}/include/libavcodec_illixr
mv ${CMAKE_INSTALL_PREFIX}/include/libavdevice ${CMAKE_INSTALL_PREFIX}/include/libavdevice_illixr
mv ${CMAKE_INSTALL_PREFIX}/include/libavfilter ${CMAKE_INSTALL_PREFIX}/include/libavfilter_illixr
mv ${CMAKE_INSTALL_PREFIX}/include/libavformat ${CMAKE_INSTALL_PREFIX}/include/libavformat_illixr
mv ${CMAKE_INSTALL_PREFIX}/include/libavutil ${CMAKE_INSTALL_PREFIX}/include/libavutil_illixr
mv ${CMAKE_INSTALL_PREFIX}/include/libswresample ${CMAKE_INSTALL_PREFIX}/include/libswresample_illixr
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
                        GIT_TAG 409e9f50999df7410fa9c0a13d97106e45c11b3b
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/ffmpeg
                        DEPENDS ${Vulkan_DEP_STR}
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                        BUILD_COMMAND cmake --build . --parallel ${BUILD_PARALLEL_LEVEL}
                        INSTALL_COMMAND ${CMAKE_BINARY_DIR}/finalize_install.sh)
    set(FFMPLIBS avcodec;avdevice;avformat;avutil;swscale)
    foreach(LIBRARY IN LISTS FFMPLIBS)
        set(lib${LIBRARY}_illixr_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_PREFIX ${CMAKE_INSTALL_PREFIX} CACHE INTERNAL "" FORCE)
        set(lib${LIBRARY}_illixr_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib CACHE INTERNAL "" FORCE)
    endforeach()

    set(libavcodec_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-ldl;-lstdc++;-l-latomic;-lX11 CACHE INTERNAL "" FORCE)
    set(libavcodec_illixr_LIBRARIES avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;dl;stdc++;atomic;X11 CACHE INTERNAL "" FORCE)

    set(libavdevice_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavdevice_illixr;-lm;-latomic;-lraw1394;-liec61883;-lpthread;-ldrm;-lxcb;-lxcb-shm;-lGL;-lpulse;-pthread;-lSDL2;-lv4l2;-lX11;-lXext;-lavfilter_illixr;-pthread;-lm;-latomic;-lbs2b;-lass;-lvidstab;-lm;-lgomp;-lzimg;-l-lfontconfig;-lfreetype;-ldl;-lstdc++;-lswscale_illixr;-lm;-latomic;-lm;-latomic;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;;-lssh;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-ldl;-lstdc++;-l-latomic;-lX11 CACHE INTERNAL "" FORCE)
    set(libavdevice_illixr_LIBRARIES avdevice_illixr;m;atomic;raw1394;iec61883;pthread;drm;xcb;xcb-shm;GL;pulse;SDL2;v4l2;X11;Xext;avfilter_illixr;m;atomic;bs2b;ass;vidstab;m;gomp;zimg;fontconfig;freetype;dl;stdc++;swscale_illixr;m;atomic;m;atomic;avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;ssh;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;;dl;stdc++;atomic;X11 CACHE INTERNAL "" FORCE)

    set(libavformat_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lssh;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-ldl;-lstdc++;-l-latomic;-lX11 CACHE INTERNAL "" FORCE)
    set(libavformat_illixr_LIBRARIES avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;ssh;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;dl;stdc++;atomic;X11 CACHE INTERNAL "" FORCE)

    set(libavutil_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-ldl;-lstdc++;-l-latomic;-lX11 CACHE INTERNAL "" FORCE)
    set(libavutil_illixr_LIBRARIES avutil_illixr;vdpau;X11;m;drm;dl;stdc++;atomic;X11 CACHE INTERNAL "" FORCE)

    set(libswscale_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lswscale_illixr;-lm;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-ldl;-lstdc++;-l-latomic;-lX11 CACHE INTERNAL "" FORCE)
    set(libswscale_illixr_LIBRARIES swscale_illixr;m;atomic;avutil_illixr;vdpau;X11;m;drm;dl;stdc++;atomic;X11 CACHE INTERNAL "" FORCE)

    set(ILLIXR_FFmpeg_EXTERNAL Yes)
    set(ILLIXR_FFmpeg_DEP_STR ILLIXR_FFmpeg_ext)
else()
    message("FFMPEG FOUND")
endif()

