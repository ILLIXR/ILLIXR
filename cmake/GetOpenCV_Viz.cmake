# module to build OpenCV from source
# this is only needed on CentOS systems because they do not supply all of the needed parts of OpenCV in the
#  system repos

find_package(VTK REQUIRED)

EXTERNALPROJECT_ADD(OpenCV_Viz
        GIT_REPOSITORY https://github.com/ILLIXR/opencv.git   # Git repo for source code
        GIT_TAG 7778f23491863f2ffceca1894579a53a072747e1      # sha5 hash for specific commit to pull (if there is no specific tag to use)
        UPDATE_COMMAND git submodule update --init            # make sure the submodules are updates
        PREFIX ${CMAKE_BINARY_DIR}/_deps/OpenCV_Viz           # the build directory
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_JAVA=OFF -DWITH_OPENGL=ON -DWITH_VTK=ON -DOPENCV_EXTRA_MODULES_PATH=${CMAKE_BINARY_DIR}/_deps/OpenCV_Viz/src/OpenCV_Viz/opencv_contrib/modules
        )

# set variables for use by modules that depend on this one
set(OpenCV_FOUND 1)
set(OpenCV_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/opencv4)
set(OpenCV_LIBRARIES opencv_alphamat;opencv_dpm;opencv_imgproc;opencv_reg;opencv_video;opencv_aruco;opencv_face;opencv_intensity_transform;opencv_rgbd;opencv_videoio;opencv_barcode;opencv_features2d;opencv_line_descriptor;opencv_saliency;opencv_videostab;opencv_bgsegm;opencv_flann;opencv_mcc;opencv_sfm;opencv_viz;opencv_bioinspired;opencv_freetype;opencv_ml;opencv_shape;opencv_wechat_qrcode;opencv_ca3d;opencv_fuzzy;opencv_objdetect;opencv_stereo;opencv_xfeatures2d;opencv_cca;opencv_gapi;opencv_optflow;opencv_stitching;opencv_ximgproc;opencv_core;opencv_hdf;opencv_phase_unwrapping;opencv_structured_light;opencv_xobjdetect;opencv_datasets;opencv_hfs;opencv_photo;opencv_superres;opencv_xphoto;opencv_dnn;opencv_highgui;opencv_plot;opencv_surface_matching;opencv_dnn_objdetect;opencv_img_hash;opencv_quality;opencv_text;opencv_dnn_superres;opencv_imgcodecs;opencv_rapid;opencv_tracking)
set(OpenCV_LIBS ${OpenCV_LIBRARIES})
set(OpenCV_LIB_COMPONENTS ${OpenCV_LIBRARIES})
set(OpenCV_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib)
set(OpenCV_DIR ${CMAKE_INSTALL_PREFIX}/lib/cmake/opencv4)
message("OpenCV configured")

