#pragma once

#include "error_util.hpp"
#include "global_module_defs.hpp"
#include "phonebook.hpp"

#include <cassert>
#include <cerrno>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <GL/glew.h>

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092

#else
#include <GL/glx.h>
    // GLX context magics
    #define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
    #define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#endif

#ifndef NDEBUG
    #include <spdlog/spdlog.h>
#endif


#if defined(_WIN32) || defined(_WIN64)
typedef HGLRC(WINAPI* wglCreateContextAttribsARBProc)(HDC hDC, HGLRC hShareContext, const int* attribList);
#else
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
#endif

namespace ILLIXR {
class xlib_gl_extended_window : public phonebook::service {
public:
#if defined(_WIN32) || defined(_WIN64)
    xlib_gl_extended_window(int width, int height, HGLRC shared_gl_context) {
#else
    xlib_gl_extended_window(int width, int height, GLXContext shared_gl_context) {
#endif
        width_  = width;
        height_ = height;

#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Opening display");
#endif
        RAC_ERRNO_MSG("extended_window at start of xlib_gl_extended_window constructor");

#if defined(_WIN32) || defined(_WIN64)
        HINSTANCE    hInstance  = GetModuleHandle(nullptr);
        const TCHAR* class_name = TEXT("ILLIXR_Extended_Window_Class");

        // Register a window class
        WNDCLASS wc      = {};
        if (!GetClassInfo(hInstance, class_name, &wc)) {
            wc.style         = CS_OWNDC;
            wc.lpfnWndProc   = DefWindowProc;
            wc.hInstance     = hInstance;
            wc.lpszClassName = class_name;

            if (!RegisterClass(&wc)) {
                ILLIXR::abort("RegisterClass failed");
            }
        }
        // Create the window
        hwnd_ = CreateWindow(wc.lpszClassName, TEXT("ILLIXR Extended Window"), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                             CW_USEDEFAULT, width_, height_, nullptr, nullptr, wc.hInstance, nullptr);

        if (!hwnd_) {
            ILLIXR::abort("CreateWindow failed");
        }

        // Get device context
        hdc_ = GetDC(hwnd_);

        // Choose a pixel format
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize                 = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion              = 1;
        pfd.dwFlags               = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType            = PFD_TYPE_RGBA;
        pfd.cColorBits            = 32;
        pfd.cDepthBits            = 24;
        pfd.cStencilBits          = 8;
        pfd.iLayerType            = PFD_MAIN_PLANE;
#else
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
#endif
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Getting matching framebuffer configs");
#endif
        RAC_ERRNO_MSG("extended_window before glXChooseFBConfig");

#if defined(_WIN32) || defined(_WIN64)
        int pixel_format = ChoosePixelFormat(hdc_, &pfd);
        if (pixel_format == 0) {
            ILLIXR::abort("ChoosePixelFormat failed");
        }

         SetPixelFormat(hdc_, pixel_format, &pfd);

        // Create a dummy context so we can load extensions
         HGLRC temp_context = wglCreateContext(hdc_);
         wglMakeCurrent(hdc_, temp_context);

         // Load extension
         auto wglCreateContextAttribsARB = (wglCreateContextAttribsARBProc) wglGetProcAddress("wglCreateContextAttribsARB");

         if (!wglCreateContextAttribsARB) {
             ILLIXR::abort("wglCreateContextAttribsARB not available, using legacy context");
             context_ = temp_context;
         } else {
             int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, 0};
             context_      = wglCreateContextAttribsARB(hdc_, shared_gl_context, attribs);
             // Replace dummy
             wglMakeCurrent(nullptr, nullptr);
             wglDeleteContext(temp_context);
             wglMakeCurrent(hdc_, context_);
         }
#else
        int          fb_count  = 0;
        int          screen    = DefaultScreen(display_);
        GLXFBConfig* fb_config = glXChooseFBConfig(display_, screen, visual_attribs, &fb_count);
        if (!fb_config) {
            ILLIXR::abort("Failed to retrieve a framebuffer config");
        }
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Found {} matching FB configs", fb_count);

        // Pick the FB config/visual with the most samples per pixel
        spdlog::get("illixr")->debug("[extended_window] Getting XVisualInfos");
#endif
        int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;
        int i;
        for (i = 0; i < fb_count; ++i) {
            XVisualInfo* vis_info = glXGetVisualFromFBConfig(display_, fb_config[i]);
            if (vis_info) {
                int samp_buf, samples;
                glXGetFBConfigAttrib(display_, fb_config[i], GLX_SAMPLE_BUFFERS, &samp_buf);
                glXGetFBConfigAttrib(display_, fb_config[i], GLX_SAMPLES, &samples);
#ifndef NDEBUG
                spdlog::get("illixr")->debug(
                    "[extended_window] Matching fb_config {}, visual ID {:x}: SAMPLE_BUFFERS = {}, SAMPLES = {}", i,
                    vis_info->visualid, samp_buf, samples);
#endif
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
#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Chose visual ID = {:x}", vis_info->visualid);
        spdlog::get("illixr")->debug("[extended_window] Creating colormap");
#endif
        color_map_ = XCreateColormap(display_, root, vis_info->visual, AllocNone);

#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Creating window");
#endif
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

#ifndef NDEBUG
        spdlog::get("illixr")->debug("[extended_window] Creating context");
#endif
        auto glXCreateContextAttribsARB =
            (glXCreateContextAttribsARBProc) glXGetProcAddressARB((const GLubyte*) "glXCreateContextAttribsARB");
        int context_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, None};

        context_ = glXCreateContextAttribsARB(display_, g_best_fbc, shared_gl_context, True, context_attribs);

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
#endif

    }

    ~xlib_gl_extended_window() override {
        RAC_ERRNO_MSG("xlib_gl_extended_window at start of destructor");
#if defined(_WIN32) || defined(_WIN64)

        wglMakeCurrent(nullptr, nullptr);
        if (context_) {
            wglDeleteContext(context_);
        }
        if (hdc_) {
            ReleaseDC(hwnd_, hdc_);
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
        }
#else
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
#endif
        RAC_ERRNO_MSG("xlib_gl_extended_window at end of destructor");
    }

    int        width_;
    int        height_;
#if defined(_WIN32) || defined(_WIN64)
    HWND  hwnd_;
    HDC   hdc_;
    HGLRC context_;

#else
    Display*   display_;
    Window     window_;
    GLXContext context_;

private:
    Colormap color_map_;
#endif
};
} // namespace ILLIXR
