#pragma once
#if defined(_WIN32) || defined(_WIN64)
    #ifdef BUILDING_LIBRARY
        #define MY_EXPORT_API __declspec(dllexport)
    #else
        #define MY_EXPORT_API __declspec(dllimport)
    #endif
#else
    #define MY_EXPORT_API __attribute__((visibility("default")))
#endif
