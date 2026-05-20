#pragma once

#ifdef __ANDROID__

#include "illixr/phonebook.hpp"
#include "illixr/global_module_defs.hpp"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES3/gl3platform.h>
#include <jni.h>

#include <android/native_window.h>


namespace ILLIXR {
class xlib_gl_extended_window : public phonebook::service {
public:
    int width_;
    int height_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    ANativeWindow* my_window_;

    xlib_gl_extended_window(int width, int height, EGLContext egl_context, ANativeWindow* window);

    xlib_gl_extended_window(int width, int height, EGLContext egl_context);

    ~xlib_gl_extended_window();
};
}

#endif
