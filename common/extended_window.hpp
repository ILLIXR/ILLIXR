#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glu.h>
//GLX context magics
#define GLX_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB       0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
   
namespace ILLIXR{
    class xlib_gl_extended_window : public service {
    public:
        int                     width;
        int                     height;
        Display                 *dpy;
        Window                  win;
        GLXContext              glc;
        xlib_gl_extended_window(int _width, int _height, GLXContext _shared_gl_context){
            width = _width;
            height = _height;
        
            dpy = XOpenDisplay(NULL);
            if(dpy == NULL) {
                printf("\n\tcannot connect to X server\n\n");
                    exit(0);
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
                //GLX_SAMPLE_BUFFERS  , 1,
                //GLX_SAMPLES         , 4,
                None
            };
            // vi = glXChooseVisual(dpy, 0, att);
            // if(vi == NULL) {
            //  printf("\n\tno appropriate visual found\n\n");
            //         exit(0);
            // }
            // else {
            //  printf("\n\tvisual %p selected\n", (void *)vi->visualid); /* %p creates hexadecimal output like in glxinfo */
            // }

            printf( "Getting matching framebuffer configs\n" );
            int fbcount;
            GLXFBConfig* fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), visual_attribs, &fbcount);
            if (!fbc)
            {
                printf( "Failed to retrieve a framebuffer config\n" );
                exit(1);
            }
            printf( "Found %d matching FB configs.\n", fbcount );
            // Pick the FB config/visual with the most samples per pixel
            printf( "Getting XVisualInfos\n" );
            int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;
            int i;
            for (i=0; i<fbcount; ++i)
            {
                XVisualInfo *vi = glXGetVisualFromFBConfig( dpy, fbc[i] );
                if ( vi )
                {
                    int samp_buf, samples;
                    glXGetFBConfigAttrib( dpy, fbc[i], GLX_SAMPLE_BUFFERS, &samp_buf );
                    glXGetFBConfigAttrib( dpy, fbc[i], GLX_SAMPLES       , &samples  );
                    printf( "  Matching fbconfig %d, visual ID 0x%2x: SAMPLE_BUFFERS = %d,"
                            " SAMPLES = %d\n",
                            i, vi -> visualid, samp_buf, samples );
                    if ( best_fbc < 0 || samp_buf && samples > best_num_samp )
                      best_fbc = i, best_num_samp = samples;
                    if ( worst_fbc < 0 || !samp_buf || samples < worst_num_samp )
                      worst_fbc = i, worst_num_samp = samples;
                }
                XFree( vi );
            }
            GLXFBConfig bestFbc = fbc[ best_fbc ];
            // Be sure to free the FBConfig list allocated by glXChooseFBConfig()
            XFree( fbc );
            // Get a visual
            XVisualInfo *vi = glXGetVisualFromFBConfig( dpy, bestFbc );
            printf( "Chosen visual ID = 0x%x\n", vi->visualid );

            Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);
            XSetWindowAttributes swa;
            swa.colormap = cmap;
            swa.event_mask = ExposureMask | KeyPressMask;
            win = XCreateWindow(dpy, root, 0, 0, width, height, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
            XMapWindow(dpy, win);
            XStoreName(dpy, win, "ILLIXR Extended Window");

            //glc = glXCreateContext(dpy, vi, *ctx, true);
            // calling glXGetProcAddressARB
            glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
            glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
                glXGetProcAddressARB( (const GLubyte *) "glXCreateContextAttribsARB" );
            int context_attribs[] =
                {
                    GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
                    GLX_CONTEXT_MINOR_VERSION_ARB, 0,
                    //GLX_CONTEXT_FLAGS_ARB        , GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                    None
                };
            printf( "Creating context\n" );
            glc = glXCreateContextAttribsARB( dpy, bestFbc, _shared_gl_context,
                                                   True, context_attribs );
        }
    };
}