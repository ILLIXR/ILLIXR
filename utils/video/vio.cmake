
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)

set(INC_DIRS ${GLIB_INCLUDE_DIRS} ${GSTREAMER_INCLUDE_DIRS} ${GST_APP_INCLUDE_DIRS} ${GST_AUDIO_INCLUDE_DIRS} ${GST_VIDEO_INCLUDE_DIRS})
# remove duplicates
list(REMOVE_DUPLICATES INC_DIRS)

# gather the libraries
set(LINK_LIBS ${GLIB_LIBRARIES} ${GSTREAMER_LIBRARIES} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_AUDIO_LIBRARIES})
# reverse to maintain order in final list
list(REVERSE LINK_LIBS)
# remove the copious duplicates
list(REMOVE_DUPLICATES LINK_LIBS)
# reverse back to original order
list(REVERSE LINK_LIBS)

add_library(video_encoder_vio OBJECT
            video_encoder.hpp
            video_encoder.cpp
)

target_include_directories(video_encoder_vio PUBLIC
                           ${OpenCV_INCLUDE_DIRS}
                           ${INC_DIRS}
)

target_link_libraries(video_encoder_vio PUBLIC
                      ${OpenCV_LIBS}
                      ${LINK_LIBS}
)

target_compile_definitions(video_encoder_vio PUBLIC VIO)

add_library(video_decoder_vio OBJECT
            video_decoder.hpp
            video_decoder.cpp
)

target_include_directories(video_decoder_vio PUBLIC
                           ${OpenCV_INCLUDE_DIRS}
                           ${INC_DIRS}
)

target_link_libraries(video_decoder_vio PUBLIC
                      ${OpenCV_LIBS}
                      ${LINK_LIBS}
)
target_compile_definitions(video_decoder_vio PUBLIC VIO)
