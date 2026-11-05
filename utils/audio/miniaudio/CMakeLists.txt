# miniaudio, single-file audio I/O library (https://miniaud.io), used by the audio_input and audio_capture plugins.
# Only built when a target links it.
add_library(illixr_miniaudio STATIC EXCLUDE_FROM_ALL miniaudio.c)

target_include_directories(illixr_miniaudio SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/include)

# device I/O and the PCM ring buffer are all the plugins use
target_compile_definitions(illixr_miniaudio PUBLIC
                           MA_NO_DECODING
                           MA_NO_ENCODING
                           MA_NO_GENERATION
                           MA_NO_RESOURCE_MANAGER
                           MA_NO_NODE_GRAPH
                           MA_NO_ENGINE
)

# each plugin links its own copy, hidden visibility keeps the copies from colliding when both are loaded
set_target_properties(illixr_miniaudio PROPERTIES
                      POSITION_INDEPENDENT_CODE ON
                      C_VISIBILITY_PRESET hidden
)

# backends are loaded at runtime (WASAPI, PulseAudio/ALSA, AAudio/OpenSL ES), so no audio libraries are linked
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(illixr_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
