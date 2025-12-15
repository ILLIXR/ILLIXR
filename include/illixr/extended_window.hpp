#pragma once

#include "error_util.hpp"
#include "global_module_defs.hpp"
#include "phonebook.hpp"

#include <cassert>
#include <cerrno>
#ifdef ILLIXR_ANDROID_BUILD
    #include <EGL/egl.h>
    #include <GLES3/gl32.h>
    #include <GLES3/gl3ext.h>
    #include <GLES3/gl3platform.h>
#else
    #include <GL/glx.h>
#endif

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <spdlog/spdlog.h>
#ifdef ILLIXR_ANDROID_BUILD
    #include <android/log.h>
    #include <android/native_window.h>
    #include <jni.h>
    #define I_GLCONTEXT EGLContext
#else
    // GLX context magics
    #define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
    #define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    #define I_GLCONTEXT                   GLXContext
#endif
namespace ILLIXR {
class xlib_gl_extended_window : public phonebook::service {
public:
    xlib_gl_extended_window(int width, int height, I_GLCONTEXT gl_context
#ifdef ILLIXR_ANDROID_BUILD
                            ,
                            ANativeWindow* window = nullptr) {
        window_ = window;
#else
    ) {
#endif
        width_  = width;
        height_ = height;

#ifdef ILLIXR_ANDROID_BUILD
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major_version, minor_version;
        eglInitialize(display_, &major_version, &minor_version);
        spdlog::get("illixr")->info("EGL Initialized with major version : {} minor version: {}", major_version, minor_version);

        const EGLint attribs[] = {// EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                  EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_ES2_BIT,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_ALPHA_SIZE,
                                  0, // EGL_DEPTH_SIZE, 24,
                                  EGL_NONE};

        [[maybe_unused]] EGLint w;
        [[maybe_unused]] EGLint h;
        EGLint                  format;
        EGLint                  numConfigs;
        EGLConfig               config = nullptr;
        eglChooseConfig(display_, attribs, &config, 1, &numConfigs);
        std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
        assert(supportedConfigs);
        eglChooseConfig(display_, attribs, supportedConfigs.get(), numConfigs, &numConfigs);
        assert(numConfigs);
        auto i = 0;
        for (; i < numConfigs; i++) {
            auto&  cfg = supportedConfigs[i];
            EGLint r, g, b, d;
            if (eglGetConfigAttrib(display_, cfg, EGL_RED_SIZE, &r) && eglGetConfigAttrib(display_, cfg, EGL_GREEN_SIZE, &g) &&
                eglGetConfigAttrib(display_, cfg, EGL_BLUE_SIZE, &b) && eglGetConfigAttrib(display_, cfg, EGL_DEPTH_SIZE, &d) &&
                r == 8 && g == 8 && b == 8 && d == 24) {
                config = supportedConfigs[i];
                break;
            }
        }
        if (i == numConfigs) {
            config = supportedConfigs[0];
        }

        if (config == nullptr) {
            return;
        }
        eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &format);
        if (window_ != nullptr) {
            try {
                surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
            } catch (const std::exception& e) {
                return;
            }
            spdlog::get("illixr")->info("window is not nullptr");
        } else {
            surface_ = EGL_NO_SURFACE;
            spdlog::get("illixr")->info("window is nullptr");
        }
        EGLint ctxattrb[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_          = eglCreateContext(display_, config, egl_context, ctxattrb);

        if (window_ != nullptr) {
            eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
            eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
        }
#else
        spdlog::get("illixr")->debug("[extended_window] Opening display");

        RAC_ERRNO_MSG("extended_window at start of xlib_gl_extended_window constructor");

        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            ILLIXR::abort("Cannot connect to X server");
        } else {
            // Apparently, XOpenDisplay's _true_ error indication is whether display_ is nullptr.
            // https://cboard.cprogramming.com/linux-programming/119957-xlib-perversity.html
            // if (errno != 0) {
            // 	std::cerr << "XOpenDisplay succeeded, but errno = " << errno << "; This is benign, so I'm clearing it now.\n";
            // }
            errno = 0;
        }

        Window root = DefaultRootWindow(display_);
        // Get a matching FB config
        static int visual_attribs[] = {GLX_X_RENDERABLE,
                                       True,
                                       GLX_DRAWABLE_TYPE,
                                       GLX_WINDOW_BIT,
                                       GLX_RENDER_TYPE,
                                       GLX_RGBA_BIT,
                                       GLX_X_VISUAL_TYPE,
                                       GLX_TRUE_COLOR,
                                       GLX_RED_SIZE,
                                       8,
                                       GLX_GREEN_SIZE,
                                       8,
                                       GLX_BLUE_SIZE,
                                       8,
                                       GLX_ALPHA_SIZE,
                                       8,
                                       GLX_DEPTH_SIZE,
                                       24,
                                       GLX_STENCIL_SIZE,
                                       8,
                                       GLX_DOUBLEBUFFER,
                                       True,
                                       None};

        spdlog::get("illixr")->debug("[extended_window] Getting matching framebuffer configs");

        RAC_ERRNO_MSG("extended_window before glXChooseFBConfig");

        int          fb_count  = 0;
        int          screen    = DefaultScreen(display_);
        GLXFBConfig* fb_config = glXChooseFBConfig(display_, screen, visual_attribs, &fb_count);
        if (!fb_config) {
            ILLIXR::abort("Failed to retrieve a framebuffer config");
        }

        spdlog::get("illixr")->debug("[extended_window] Found {} matching FB configs", fb_count);

        // Pick the FB config/visual with the most samples per pixel
        spdlog::get("illixr")->debug("[extended_window] Getting XVisualInfos");

        int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;
        int i;
        for (i = 0; i < fb_count; ++i) {
            XVisualInfo* vis_info = glXGetVisualFromFBConfig(display_, fb_config[i]);
            if (vis_info) {
                int samp_buf, samples;
                glXGetFBConfigAttrib(display_, fb_config[i], GLX_SAMPLE_BUFFERS, &samp_buf);
                glXGetFBConfigAttrib(display_, fb_config[i], GLX_SAMPLES, &samples);

                spdlog::get("illixr")->debug(
                    "[extended_window] Matching fb_config {}, visual ID {:x}: SAMPLE_BUFFERS = {}, SAMPLES = {}", i,
                    vis_info->visualid, samp_buf, samples);

                if (best_fbc < 0 || (samp_buf && samples > best_num_samp)) {
                    best_fbc = i, best_num_samp = samples;
                }
                if (worst_fbc < 0 || (!samp_buf || samples < worst_num_samp)) {
                    worst_fbc = i, worst_num_samp = samples;
                }
            }
            XFree(vis_info);
        }

        assert(0 <= best_fbc && best_fbc < fb_count);
        GLXFBConfig g_best_fbc = fb_config[best_fbc];

        // Free the FBConfig list allocated by glXChooseFBConfig()
        XFree(fb_config);

        // Get a visual
        XVisualInfo* vis_info = glXGetVisualFromFBConfig(display_, g_best_fbc);

        spdlog::get("illixr")->debug("[extended_window] Chose visual ID = {:x}", vis_info->visualid);
        spdlog::get("illixr")->debug("[extended_window] Creating colormap");

        color_map_ = XCreateColormap(display_, root, vis_info->visual, AllocNone);

        spdlog::get("illixr")->debug("[extended_window] Creating window");

        XSetWindowAttributes attributes;
        attributes.colormap         = color_map_;
        attributes.background_pixel = 0;
        attributes.border_pixel     = 0;
        attributes.event_mask       = ExposureMask | KeyPressMask;
        window_ = XCreateWindow(display_, root, 0, 0, width_, height_, 0, vis_info->depth, InputOutput, vis_info->visual,
                                CWBackPixel | CWColormap | CWBorderPixel | CWEventMask, &attributes);
        if (!window_) {
            ILLIXR::abort("Failed to create window");
        }
        XStoreName(display_, window_, "ILLIXR Extended Window");
        XMapWindow(display_, window_);

        // Done with visual info
        XFree(vis_info);

        spdlog::get("illixr")->debug("[extended_window] Creating context");

        auto glXCreateContextAttribsARB =
            (glXCreateContextAttribsARBProc) glXGetProcAddressARB((const GLubyte*) "glXCreateContextAttribsARB");
        int context_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, None};

        context_ = glXCreateContextAttribsARB(display_, g_best_fbc, gl_context, True, context_attribs);

        // Sync to process errors
        RAC_ERRNO_MSG("extended_window before XSync");
        XSync(display_, false);
        RAC_ERRNO_MSG("extended_window after XSync");

    #ifndef NDEBUG
            // Doing glXMakeCurrent here makes a third thread, the runtime one, enter the mix, and
            // then there are three GL threads: runtime, timewarp, and gldemo, and the switching of
            // contexts without synchronization during the initialization phase causes a data race.
            // This is why native.yaml sometimes succeeds and sometimes doesn't. Headless succeeds
            // because this behavior is OpenGL implementation dependent, and apparently mesa
            // differs from NVIDIA in this regard.
            // The proper fix is #173. Comment the below back in once #173 is done. In any case,
            // this is just for debugging and does not affect any functionality.

            /*
            const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(display_, window_, context_));
            int major = 0, minor = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            std::cout << "OpenGL context created" << std::endl
                      << "Version " << major << "." << minor << std::endl
                      << "Vendor " << glGetString(GL_VENDOR) << std::endl
                      << "Renderer " << glGetString(GL_RENDERER) << std::endl;
            const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(display_, None, nullptr));
            */
    #endif
    }

    ~xlib_gl_extended_window() override {
    #ifdef ILLIXR_ANDROID_BUILD
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
            }
            if (surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, surface_);
            }
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY;
        context_ = EGL_NO_CONTEXT;
        surface_ = EGL_NO_SURFACE;
    #else
        RAC_ERRNO_MSG("xlib_gl_extended_window at start of destructor");

        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(display_, None, nullptr));
        assert(gl_result && "glXMakeCurrent should not fail");

        glXDestroyContext(display_, context_);
        XDestroyWindow(display_, window_);
        Window root = DefaultRootWindow(display_);
        XDestroyWindow(display_, root);
        XFreeColormap(display_, color_map_);

        /// See [documentation](https://tronche.com/gui/x/xlib/display/XCloseDisplay.html)
        /// See [example](https://www.khronos.org/opengl/wiki/Programming_OpenGL_in_Linux:_GLX_and_Xlib)
        XCloseDisplay(display_);

        RAC_ERRNO_MSG("xlib_gl_extended_window at end of destructor");
    #endif
    }

    int width_;
    int height_;
    #ifdef ILLIXR_ANDROID_BUILD
    EGLDisplay     display_;
    EGLSurface     surface_;
    EGLContext     context_;
    ANativeWindow* window_;
    #else
    Display*   display_;
    Window     window_;
    GLXContext context_;
    #endif

private:
    Colormap color_map_;
};
} // namespace ILLIXR
