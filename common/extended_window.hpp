#pragma once

#include <cerrno>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glu.h>
#include "phonebook.hpp"
#include "global_module_defs.hpp"
#include "error_util.hpp"

//GLX context magics
#define GLX_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB       0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

namespace ILLIXR {
    class xlib_gl_extended_window : public phonebook::service {
    public:
        int        width;
        int        height;
        Display    *dpy;
        Window     win;
        GLXContext glc;
        xlib_gl_extended_window(int _width, int _height, GLXContext _shared_gl_context){
            width = _width;
            height = _height;

#ifndef NDEBUG
            std::cout << "Opening display" << std::endl;
#endif
            assert(errno == 0 && "Errno should not be set at start of xlib_gl_extended_window constructor");

            dpy = XOpenDisplay(nullptr);
            if (!dpy) {
                ILLIXR::abort("Cannot connect to X server");
            } else {
				// Apparently, XOpenDisplay's _true_ error indication is whether dpy is nullptr.
				// https://cboard.cprogramming.com/linux-programming/119957-xlib-perversity.html
				// if (errno != 0) {
				// 	std::cerr << "XOpenDisplay succeeded, but errno = " << errno << "; This is benign, so I'm clearing it now.\n";
				// }
				errno = 0;
            }

            Window root = DefaultRootWindow(dpy);
            // Get a matching FB config
            static int visual_attribs[] =
            {
                GLX_X_RENDERABLE    , True,
                GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
                GLX_RENDER_TYPE     , GLX_RGBA_BIT,
                GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
                GLX_RED_SIZE        , 8,
                GLX_GREEN_SIZE      , 8,
                GLX_BLUE_SIZE       , 8,
                GLX_ALPHA_SIZE      , 8,
                GLX_DEPTH_SIZE      , 24,
                GLX_STENCIL_SIZE    , 8,
                GLX_DOUBLEBUFFER    , True,
                None
            };

#ifndef NDEBUG
            std::cout << "Getting matching framebuffer configs" << std::endl;
#endif
            assert(errno == 0 && "Errno should not be set before glXChooseFBConfig");

            int fbcount = 0;
            GLXFBConfig* fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), visual_attribs, &fbcount);
            if (!fbc) {
                ILLIXR::abort("Failed to retrieve a framebuffer config");
            }

#ifndef NDEBUG
            std::cout << "Found " << fbcount << " matching FB configs" << std::endl;

            // Pick the FB config/visual with the most samples per pixel
            std::cout << "Getting XVisualInfos" << std::endl;
#endif
            int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;
            int i;
            for (i = 0; i < fbcount; ++i) {
                XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, fbc[i]);
                if (vi) {
                    int samp_buf, samples;
                    glXGetFBConfigAttrib(dpy, fbc[i], GLX_SAMPLE_BUFFERS, &samp_buf);
                    glXGetFBConfigAttrib(dpy, fbc[i], GLX_SAMPLES       , &samples );
#ifndef NDEBUG
                    std::cout << "Matching fbconfig " << i
                              << ", visual ID 0x" << std::hex << vi->visualid << std::dec
                              << ": SAMPLE_BUFFERS = " << samp_buf
                              << ", SAMPLES = " << samples
                              << std::endl;
#endif
                    if (best_fbc < 0 || (samp_buf && samples > best_num_samp))
                      best_fbc = i, best_num_samp = samples;
                    if (worst_fbc < 0 || !samp_buf || samples < worst_num_samp)
                      worst_fbc = i, worst_num_samp = samples;
                }
                XFree(vi);
            }

            GLXFBConfig bestFbc = fbc[best_fbc];

            // Free the FBConfig list allocated by glXChooseFBConfig()
            XFree(fbc);

            // Get a visual
            XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, bestFbc);
#ifndef NDEBUG
            std::cout << "Chose visual ID = 0x" << std::hex << vi->visualid << std::dec << std::endl;

            std::cout << "Creating colormap" << std::endl;
#endif
            Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);

#ifndef NDEBUG
            std::cout << "Creating window" << std::endl;
#endif
            XSetWindowAttributes attributes;
            attributes.colormap = cmap;
            attributes.background_pixel = 0;
            attributes.border_pixel = 0;
            attributes.event_mask = ExposureMask | KeyPressMask;
            win = XCreateWindow(dpy, root, 0, 0, width, height, 0, vi->depth, InputOutput, vi->visual,
                CWBackPixel | CWColormap | CWBorderPixel | CWEventMask, &attributes);
            if (!win) {
                ILLIXR::abort("Failed to create window");
            }
            XStoreName(dpy, win, "ILLIXR Extended Window");
            XMapWindow(dpy, win);

            // Done with visual info
            XFree(vi);

#ifndef NDEBUG
            std::cout << "Creating context" << std::endl;
#endif
            glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
            glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
                glXGetProcAddressARB((const GLubyte *) "glXCreateContextAttribsARB");
            int context_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                None
            };

            glc = glXCreateContextAttribsARB(dpy, bestFbc, _shared_gl_context, True, context_attribs);

            // Sync to process errors
            RAC_ERRNO_MSG("extended_window before XSync");
            XSync(dpy, false);
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
            const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(dpy, win, glc));
            int major = 0, minor = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            std::cout << "OpenGL context created" << std::endl
                      << "Version " << major << "." << minor << std::endl
                      << "Vender " << glGetString(GL_VENDOR) << std::endl
                      << "Renderer " << glGetString(GL_RENDERER) << std::endl;
            const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(dpy, None, nullptr));
            */
#endif
        }
    };
}
