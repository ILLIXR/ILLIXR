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

if(NOT (libavcodec_illixr_FOUND AND libavdevice_illixr_FOUND AND
        libavformat_illixr_FOUND AND libavutil_illixr_FOUND AND libswscale_illixr_FOUND))
    EXTERNALPROJECT_ADD(ILLIXR_FFmpeg_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/FFmpeg.git
                        GIT_TAG 1c697ac6ca9134c8c3c9899a12794cb6869aa49b
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/ffmpeg
                        DEPENDS ${Vulkan_DEP_STR}
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                        )
    set(FFMPLIBS avcodec;avdevice;avformat;avutil;swscale)
    foreach(LIBRARY IN LISTS FFMPLIBS})
        set(lib${LIBRARY}_illixr_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
        set(lib${LIBRARY}_illixr_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include)
        set(lib${LIBRARY}_illixr_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
        set(lib${LIBRARY}_illixr_PREFIX ${CMAKE_INSTALL_PREFIX})
        set(lib${LIBRARY}_illixr_STATIC_CFLAGS -I${CMAKE_INSTALL_PREFIX}/include)
        set(lib${LIBRARY}_illixr_STATIC_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include})
        set(lib${LIBRARY}_illixr_STATIC_LDFLAGS_OTHER -pthread)
        set(lib${LIBRARY}_illixr_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib)
        set(lib${LIBRARY}_illixr_STATIC_LIBRARY_DIRS ${ILLIXR_FFmpeg_LIBRARY_DIRS})
    endforeach()

    set(libavcodec_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lgsm;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-lvpl;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lvpl;-ldl;-lstdc++;-lOpenCL;-latomic;-lX11)
    set(libavcodec_illixr_STATIC_LDFLAGS ${libavcodec_illixr_LDFLAGS})
    set(libavcodec_illixr_LIBRARIES avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;gsm;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;vpl;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;vpl;dl;stdc++;OpenCL;atomic;X11)
    set(libavcodec_illixr_STATIC_LIBRARIES ${libavcodec_illixr_LIBRARIES})

    set(libavdevice_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavdevice_illixr;-lm;-latomic;-lraw1394;-lavc1394;-lrom1394;-liec61883;-ljack;-lpthread;-ldrm;-lxcb;-lxcb-shm;-lasound;-lGL;-lpulse;-pthread;-lSDL2;-lsndio;-lv4l2;-lXv;-lX11;-lXext;-lavfilter_illixr;-pthread;-lm;-latomic;-lbs2b;-lass;-lvidstab;-lm;-lgomp;-lzimg;-lOpenCL;-lfontconfig;-lfreetype;-lvpl;-ldl;-lstdc++;-lswscale_illixr;-lm;-latomic;-lpostproc_illixr;-lm;-latomic;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lsrt;-lssh;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lgsm;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-lvpl;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lvpl;-ldl;-lstdc++;-lOpenCL;-latomic;-lX11)
    set(libavdevice_illixr_STATIC_LDFLAGS ${libavdevice_illixr_LDFLAGS})
    set(libavdevice_illixr_LIBRARIES avdevice_illixr;m;atomic;raw1394;avc1394;rom1394;iec61883;jack;pthread;drm;xcb;xcb-shm;asound;GL;pulse;SDL2;sndio;v4l2;Xv;X11;Xext;avfilter_illixr;m;atomic;bs2b;ass;vidstab;m;gomp;zimg;OpenCL;fontconfig;freetype;vpl;dl;stdc++;swscale_illixr;m;atomic;postproc_illixr;m;atomic;avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;srt;ssh;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;gsm;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;vpl;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;vpl;dl;stdc++;OpenCL;atomic;X11)
    set(libavdevice_illixr_STATIC_LIBRARIES ${libavdevice_illixr_LIBRARIES})

    set(libavformat_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavformat_illixr;-lm;-latomic;-lxml2;-lbz2;-lmodplug;-lopenmpt;-lstdc++;-lbluray;-lgmp;-lz;-lgnutls;-lsrt;-lssh;-lavcodec_illixr;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lvpx;-lm;-lwebpmux;-lwebp;-pthread;-lm;-latomic;-llzma;-ldav1d;-lopencore-amrwb;-lrsvg-2;-lm;-lgio-2.0;-lgdk_pixbuf-2.0;-lgobject-2.0;-lglib-2.0;-lcairo;-laom;-lgsm;-lmp3lame;-lm;-lopencore-amrnb;-lopenjp2;-lopus;-lspeex;-ltheoraenc;-ltheoradec;-logg;-lvorbis;-lvorbisenc;-lwebp;-lx264;-lx265;-lxvidcore;-lz;-lvpl;-ldl;-lstdc++;-lswresample_illixr;-lm;-lsoxr;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lvpl;-ldl;-lstdc++;-lOpenCL;-latomic;-lX11)
    set(libavformat_illixr_STATIC_LDFLAGS ${libavformat_illixr_LDFLAGS})
    set(libavformat_illixr_LIBRARIES avformat_illixr;m;atomic;xml2;bz2;modplug;openmpt;stdc++;bluray;gmp;z;gnutls;srt;ssh;avcodec_illixr;vpx;m;vpx;m;vpx;m;vpx;m;webpmux;webp;m;atomic;lzma;dav1d;opencore-amrwb;rsvg-2;m;gio-2.0;gdk_pixbuf-2.0;gobject-2.0;glib-2.0;cairo;aom;gsm;mp3lame;m;opencore-amrnb;openjp2;opus;speex;theoraenc;theoradec;ogg;vorbis;vorbisenc;webp;x264;x265;xvidcore;z;vpl;dl;stdc++;swresample_illixr;m;soxr;atomic;avutil_illixr;vdpau;X11;m;drm;vpl;dl;stdc++;OpenCL;atomic;X11)
    set(libavformat_illixr_STATIC_LIBRARIES ${libavformat_illixr_LIBRARIES})

    set(libavutil_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lvpl;-ldl;-lstdc++;-lOpenCL;-latomic;-lX11)
    set(libavutil_illixr_STATIC_LDFLAGS ${libavutil_illixr_LDFLAGS})
    set(libavutil_illixr_LIBRARIES avutil_illixr;vdpau;X11;m;drm;vpl;dl;stdc++;OpenCL;atomic;X11)
    set(libavutil_illixr_STATIC_LIBRARIES ${libavutil_illixr_LIBRARIES})

    set(libswscale_illixr_LDFLAGS -L${CMAKE_INSTALL_PREFIX}/lib;-lswscale_illixr;-lm;-latomic;-lavutil_illixr;-pthread;-lvdpau;-lX11;-lm;-ldrm;-lvpl;-ldl;-lstdc++;-lOpenCL;-latomic;-lX11)
    set(libswscale_illixr_STATIC_LDFLAGS ${libswscale_illixr_LDFLAGS})
    set(libswscale_illixr_LIBRARIES swscale_illixr;m;atomic;avutil_illixr;vdpau;X11;m;drm;vpl;dl;stdc++;OpenCL;atomic;X11)
    set(libswscale_illixr_STATIC_LIBRARIES ${libswscale_illixr_LIBRARIES})

    set(ILLIXR_FFmpeg_EXTERNAL Yes)
    set(ILLIXR_FFmpeg_DEP_STR "ILLIXR_FFmpeg_ext")
endif()

