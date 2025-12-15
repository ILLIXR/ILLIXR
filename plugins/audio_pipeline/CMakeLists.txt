if (ANDROID)
    message(FATAL_ERROR "Audio_pipeline is not for Android systems yet.")
endif()
include(GetAudio_Pipeline)
