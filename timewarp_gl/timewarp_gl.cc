#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"
#include "common/shader_util.hh"
#include "utils/algebra.hh"
#include "utils/hmd.hh"
#include "shaders/basic_shader.hh"
#include "shaders/timewarp_shader.hh"
#include "common/linalg.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glu.h>

using namespace ILLIXR;
using namespace linalg::aliases;

//GLX context magics
#define GLX_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB       0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef void (*glXSwapIntervalEXTProc)(Display *dpy, GLXDrawable drawable, int interval);

// If this is defined, gldemo will use Monado-style eyebuffers
#define USE_ALT_EYE_FORMAT

typedef struct xserverWindow
{
	Display                 *dpy;
	Window                  win;
	GLXContext              glc;
	XWindowAttributes       gwa;
	XEvent                  xev;
} XWin;

class timewarp_gl : public component {

public:
	// Public constructor, create_component passes Switchboard handles ("plugs")
	// to this constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the component can read the
	// data whenever it needs to.
	#ifdef USE_ALT_EYE_FORMAT
	timewarp_gl(std::unique_ptr<reader_latest<rendered_frame_alt>>&& frame_plug,
		  std::unique_ptr<reader_latest<pose_type>>&& pose_plug,
		  std::unique_ptr<reader_latest<global_config>>&& config_plug,
		  std::unique_ptr<writer<hologram_input>>&& hologram_plug)
		: _m_eyebuffer{std::move(frame_plug)}
		, _m_pose{std::move(pose_plug)}
		, _m_config{std::move(config_plug)}
		, _m_hologram{std::move(hologram_plug)}
	{ }
	#else
	timewarp_gl(std::unique_ptr<reader_latest<rendered_frame>>&& frame_plug,
		  std::unique_ptr<reader_latest<pose_type>>&& pose_plug,
		  std::unique_ptr<reader_latest<global_config>>&& config_plug)
		: _m_eyebuffer{std::move(frame_plug)}
		, _m_pose{std::move(pose_plug)}
		, _m_config{std::move(config_plug)}
	{ }
	#endif

private:

	static constexpr int   SCREEN_WIDTH    = 448*2;
	static constexpr int   SCREEN_HEIGHT   = 320*2;

	static constexpr double DISPLAY_REFRESH_RATE = 60.0;
	static constexpr double FPS_WARNING_TOLERANCE = 0.5;
	static constexpr double DELAY_FRACTION = 0.7;

	static constexpr double RUNNING_AVG_ALPHA = 0.1;

	XWin xwin;
	rendered_frame frame;

	// Switchboard plug for application eye buffer.
	#ifdef USE_ALT_EYE_FORMAT
	std::unique_ptr<reader_latest<rendered_frame_alt>> _m_eyebuffer;
	#else
	std::unique_ptr<reader_latest<rendered_frame>> _m_eyebuffer;
	#endif

	// Switchboard plug for pose prediction.
	std::unique_ptr<reader_latest<pose_type>> _m_pose;

	// Switchboard plug for global config data, including GLFW/GPU context handles.
	std::unique_ptr<reader_latest<global_config>> _m_config;

	// Switchboard plug for sending hologram calls
	std::unique_ptr<writer<hologram_input>> _m_hologram;


	GLuint timewarpShaderProgram;
	GLuint basicShaderProgram;
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

	double lastSwapTime;
	double lastFrameTime;
	double averageFramerate = DISPLAY_REFRESH_RATE;

	HMD::hmd_info_t hmd_info;
	HMD::body_info_t body_info;

	GLuint basic_pos_attr;
	GLuint basic_uv_attr;

	GLuint basic_vao;
	GLuint basic_pos_vbo;
	GLuint basic_uv_vbo;
	GLuint basic_indices_vbo;

	GLfloat plane_vertices[8] = {  // Coordinates for the vertices of a plane.
         -1, 1,   1, 1,
          -1, -1,   1, -1 };

	GLfloat plane_uvs[8] = {  // UVs for plane
			0, 1,   1, 1,
			0, 0,   1, 0 };

	GLuint plane_indices[6] = {  // Plane indices
			0,2,3, 1,0,3 };

	// Eye sampler array
	GLuint eye_sampler_0;
	GLuint eye_sampler_1;

	// Eye index uniform
	GLuint tw_eye_index_unif;

	// VAOs
	GLuint tw_vao;

	// Position and UV attribute locations
	GLuint distortion_pos_attr;
	GLuint distortion_uv0_attr;
	GLuint distortion_uv1_attr;
	GLuint distortion_uv2_attr;

	// Distortion mesh information
	GLuint num_distortion_vertices;
	GLuint num_distortion_indices;

	// Distortion mesh CPU buffers and GPU VBO handles
	HMD::mesh_coord3d_t* distortion_positions;
	GLuint distortion_positions_vbo;
	GLuint* distortion_indices;
	GLuint distortion_indices_vbo;
	HMD::uv_coord_t* distortion_uv0;
	GLuint distortion_uv0_vbo;
	HMD::uv_coord_t* distortion_uv1;
	GLuint distortion_uv1_vbo;
	HMD::uv_coord_t* distortion_uv2;
	GLuint distortion_uv2_vbo;

	// Handles to the start and end timewarp
	// transform matrices (3x4 uniforms)
	GLuint tw_start_transform_unif;
	GLuint tw_end_transform_unif;
	// Basic perspective projection matrix
	ksAlgebra::ksMatrix4x4f basicProjection;

	// Hologram call data
	long long _hologram_seq{0};

	void initWindow(int width, int height, GLXContext ctx){
		xwin.dpy = XOpenDisplay(NULL);
		if(xwin.dpy == NULL) {
		 	printf("\n\tcannot connect to X server\n\n");
		        exit(0);
		}
		Window root = DefaultRootWindow(xwin.dpy);
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
		// vi = glXChooseVisual(xwin.dpy, 0, att);
		// if(vi == NULL) {
		// 	printf("\n\tno appropriate visual found\n\n");
		//         exit(0);
		// }
		// else {
		// 	printf("\n\tvisual %p selected\n", (void *)vi->visualid); /* %p creates hexadecimal output like in glxinfo */
		// }

		printf( "Getting matching framebuffer configs\n" );
		int fbcount;
		GLXFBConfig* fbc = glXChooseFBConfig(xwin.dpy, DefaultScreen(xwin.dpy), visual_attribs, &fbcount);
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
		  XVisualInfo *vi = glXGetVisualFromFBConfig( xwin.dpy, fbc[i] );
		  if ( vi )
		  {
		    int samp_buf, samples;
		    glXGetFBConfigAttrib( xwin.dpy, fbc[i], GLX_SAMPLE_BUFFERS, &samp_buf );
		    glXGetFBConfigAttrib( xwin.dpy, fbc[i], GLX_SAMPLES       , &samples  );

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
		XVisualInfo *vi = glXGetVisualFromFBConfig( xwin.dpy, bestFbc );
		printf( "Chosen visual ID = 0x%x\n", vi->visualid );

		Colormap cmap = XCreateColormap(xwin.dpy, root, vi->visual, AllocNone);
		XSetWindowAttributes swa;
		swa.colormap = cmap;
		swa.event_mask = ExposureMask | KeyPressMask;
		xwin.win = XCreateWindow(xwin.dpy, root, 0, 0, width, height, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
		XMapWindow(xwin.dpy, xwin.win);
		XStoreName(xwin.dpy, xwin.win, "ILLIXR Extended Window");

		//xwin.glc = glXCreateContext(xwin.dpy, vi, *ctx, true);
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
		xwin.glc = glXCreateContextAttribsARB( xwin.dpy, bestFbc, ctx,
											   True, context_attribs );
	}

	void BuildTimewarp(HMD::hmd_info_t* hmdInfo){

		// Calculate the number of vertices+indices in the distortion mesh.
		num_distortion_vertices = ( hmdInfo->eyeTilesHigh + 1 ) * ( hmdInfo->eyeTilesWide + 1 );
		num_distortion_indices = hmdInfo->eyeTilesHigh * hmdInfo->eyeTilesWide * 6;

		// Allocate memory for the elements/indices array.
		distortion_indices = (GLuint*) malloc(num_distortion_indices * sizeof(GLuint));

		// This is just a simple grid/plane index array, nothing fancy.
		// Same for both eye distortions, too!
		for ( int y = 0; y < hmdInfo->eyeTilesHigh; y++ )
		{
			for ( int x = 0; x < hmdInfo->eyeTilesWide; x++ )
			{
				const int offset = ( y * hmdInfo->eyeTilesWide + x ) * 6;

				distortion_indices[offset + 0] = (GLuint)( ( y + 0 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 0 ) );
				distortion_indices[offset + 1] = (GLuint)( ( y + 1 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 0 ) );
				distortion_indices[offset + 2] = (GLuint)( ( y + 0 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 1 ) );

				distortion_indices[offset + 3] = (GLuint)( ( y + 0 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 1 ) );
				distortion_indices[offset + 4] = (GLuint)( ( y + 1 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 0 ) );
				distortion_indices[offset + 5] = (GLuint)( ( y + 1 ) * ( hmdInfo->eyeTilesWide + 1 ) + ( x + 1 ) );
			}
		}

		// Allocate memory for the distortion coordinates.
		// These are NOT the actual distortion mesh's vertices,
		// they are calculated distortion grid coefficients
		// that will be used to set the actual distortion mesh's UV space.
		HMD::mesh_coord2d_t* tw_mesh_base_ptr = (HMD::mesh_coord2d_t *) malloc( HMD::NUM_EYES * HMD::NUM_COLOR_CHANNELS * num_distortion_vertices * sizeof( HMD::mesh_coord2d_t ) );

		// Set the distortion coordinates as a series of arrays
		// that will be written into by the BuildDistortionMeshes() function.
		HMD::mesh_coord2d_t * distort_coords[HMD::NUM_EYES][HMD::NUM_COLOR_CHANNELS] =
		{
			{ tw_mesh_base_ptr + 0 * num_distortion_vertices, tw_mesh_base_ptr + 1 * num_distortion_vertices, tw_mesh_base_ptr + 2 * num_distortion_vertices },
			{ tw_mesh_base_ptr + 3 * num_distortion_vertices, tw_mesh_base_ptr + 4 * num_distortion_vertices, tw_mesh_base_ptr + 5 * num_distortion_vertices }
		};
		HMD::BuildDistortionMeshes( distort_coords, hmdInfo );

		// Allocate memory for position and UV CPU buffers.
		for(int eye = 0; eye < HMD::NUM_EYES; eye++){
			distortion_positions = (HMD::mesh_coord3d_t *) malloc(HMD::NUM_EYES * num_distortion_vertices * sizeof(HMD::mesh_coord3d_t));
			distortion_uv0 = (HMD::uv_coord_t *) malloc(HMD::NUM_EYES * num_distortion_vertices * sizeof(HMD::uv_coord_t));
			distortion_uv1 = (HMD::uv_coord_t *) malloc(HMD::NUM_EYES * num_distortion_vertices * sizeof(HMD::uv_coord_t));
			distortion_uv2 = (HMD::uv_coord_t *) malloc(HMD::NUM_EYES * num_distortion_vertices * sizeof(HMD::uv_coord_t));
		}

		for ( int eye = 0; eye < HMD::NUM_EYES; eye++ )
		{
			for ( int y = 0; y <= hmdInfo->eyeTilesHigh; y++ )
			{
				for ( int x = 0; x <= hmdInfo->eyeTilesWide; x++ )
				{
					const int index = y * ( hmdInfo->eyeTilesWide + 1 ) + x;

					// Set the physical distortion mesh coordinates. These are rectangular/gridlike, not distorted.
					// The distortion is handled by the UVs, not the actual mesh coordinates!
					distortion_positions[eye * num_distortion_vertices + index].x = ( -1.0f + eye + ( (float)x / hmdInfo->eyeTilesWide ) );
					distortion_positions[eye * num_distortion_vertices + index].y = ( -1.0f + 2.0f * ( ( hmdInfo->eyeTilesHigh - (float)y ) / hmdInfo->eyeTilesHigh ) *
														( (float)( hmdInfo->eyeTilesHigh * hmdInfo->tilePixelsHigh ) / hmdInfo->displayPixelsHigh ) );
					distortion_positions[eye * num_distortion_vertices + index].z = 0.0f;

					// Use the previously-calculated distort_coords to set the UVs on the distortion mesh
					distortion_uv0[eye * num_distortion_vertices + index].u = distort_coords[eye][0][index].x;
					distortion_uv0[eye * num_distortion_vertices + index].v = distort_coords[eye][0][index].y;
					distortion_uv1[eye * num_distortion_vertices + index].u = distort_coords[eye][1][index].x;
					distortion_uv1[eye * num_distortion_vertices + index].v = distort_coords[eye][1][index].y;
					distortion_uv2[eye * num_distortion_vertices + index].u = distort_coords[eye][2][index].x;
					distortion_uv2[eye * num_distortion_vertices + index].v = distort_coords[eye][2][index].y;

				}
			}
		}
		// Construct a basic perspective projection
		ksAlgebra::ksMatrix4x4f_CreateProjectionFov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.1f, 0.0f );

		// This was just temporary.
		free(tw_mesh_base_ptr);

		return;
	}

	/* Calculate timewarm transform from projection matrix, view matrix, etc */
	void CalculateTimeWarpTransform( ksAlgebra::ksMatrix4x4f * transform, const ksAlgebra::ksMatrix4x4f * renderProjectionMatrix,
                                        const ksAlgebra::ksMatrix4x4f * renderViewMatrix, const ksAlgebra::ksMatrix4x4f * newViewMatrix )
	{
		// Convert the projection matrix from [-1, 1] space to [0, 1] space.
		const ksAlgebra::ksMatrix4x4f texCoordProjection =
		{ {
			{ 0.5f * renderProjectionMatrix->m[0][0],        0.0f,                                           0.0f,  0.0f },
			{ 0.0f,                                          0.5f * renderProjectionMatrix->m[1][1],         0.0f,  0.0f },
			{ 0.5f * renderProjectionMatrix->m[2][0] - 0.5f, 0.5f * renderProjectionMatrix->m[2][1] - 0.5f, -1.0f,  0.0f },
			{ 0.0f,                                          0.0f,                                           0.0f,  1.0f }
		} };

		// Calculate the delta between the view matrix used for rendering and
		// a more recent or predicted view matrix based on new sensor input.
		ksAlgebra::ksMatrix4x4f inverseRenderViewMatrix;
		ksAlgebra::ksMatrix4x4f_InvertHomogeneous( &inverseRenderViewMatrix, renderViewMatrix );

		ksAlgebra::ksMatrix4x4f deltaViewMatrix;
		ksAlgebra::ksMatrix4x4f_Multiply( &deltaViewMatrix, &inverseRenderViewMatrix, newViewMatrix );

		ksAlgebra::ksMatrix4x4f inverseDeltaViewMatrix;
		ksAlgebra::ksMatrix4x4f_InvertHomogeneous( &inverseDeltaViewMatrix, &deltaViewMatrix );

		// Make the delta rotation only.
		inverseDeltaViewMatrix.m[3][0] = 0.0f;
		inverseDeltaViewMatrix.m[3][1] = 0.0f;
		inverseDeltaViewMatrix.m[3][2] = 0.0f;

		// TODO: Major issue. The original implementation uses inverseDeltaMatrix... as expected.
		// This is because we are applying the /inverse/ of the delta between the original render
		// view matrix and the new latepose. However, when used the inverse, the transformation
		// was actually backwards. I'm not sure if this is an issue in the demo app/rendering thread,
		// or if there's something messed up with the ATW code. In any case, using the actual
		// delta matrix itself yields the correct result, which is very odd. Must investigate!

		deltaViewMatrix.m[3][0] = 0.0f;
		deltaViewMatrix.m[3][1] = 0.0f;
		deltaViewMatrix.m[3][2] = 0.0f;

		// Accumulate the transforms.
		//ksAlgebra::ksMatrix4x4f_Multiply( transform, &texCoordProjection, &inverseDeltaViewMatrix );
		ksAlgebra::ksMatrix4x4f_Multiply( transform, &texCoordProjection, &deltaViewMatrix );
	}

	// Get the estimated time of the next swap/next Vsync.
	// This is an estimate, used to wait until *just* before vsync.
	double GetNextSwapTimeEstimate(){
		return lastSwapTime + (1.0/DISPLAY_REFRESH_RATE);
	}

	// Get the estimated amount of time to put the CPU thread to sleep,
	// given a specified percentage of the total Vsync period to delay.
	double EstimateTimeToSleep(double framePercentage){
		return (GetNextSwapTimeEstimate() - glfwGetTime()) * framePercentage;
	}


public:

	void main_loop() {
		lastSwapTime = glfwGetTime();
		while (!_m_terminate.load()) {
			using namespace std::chrono_literals;
			// Sleep for approximately 90% of the time until the next vsync.
			// Scheduling granularity can't be assumed to be super accurate here,
			// so don't push your luck (i.e. don't wait too long....) Tradeoff with
			// MTP here. More you wait, closer to the display sync you sample the pose.
			// TODO use more precise sleep.
			double sleep_start = glfwGetTime();
			// TODO: poll GLX window events
			std::this_thread::sleep_for(std::chrono::duration<double>(EstimateTimeToSleep(DELAY_FRACTION)));
			warp(glfwGetTime());
		}
	}
	/* compatibility interface */

	// ATW/Compositor overrides _p_start to control its own lifecycle/scheduling.
	// This may be changed later, but for precision of vsync/etc we're going to
	// use component-driven loop scheduling for now.
	virtual void _p_start() override {

		auto fetched_config = _m_config->get_latest_ro();
		if(!fetched_config){
			std::cerr << "Timewarp failed to fetch global config." << std::endl;
		}

		// Generate reference HMD and physical body dimensions
    	HMD::GetDefaultHmdInfo(SCREEN_WIDTH, SCREEN_HEIGHT, &hmd_info);
		HMD::GetDefaultBodyInfo(&body_info);

		// Initialize the GLFW library, still need it to get time
		if(!glfwInit()){
			printf("Failed to initialize glfw\n");
		}

    	// Construct timewarp meshes and other data
    	BuildTimewarp(&hmd_info);

		// includes setting swap interval
		initWindow(SCREEN_WIDTH, SCREEN_HEIGHT, (GLXContext)fetched_config->glctx);
		glXMakeCurrent(xwin.dpy, xwin.win, xwin.glc);

		// set swap interval for 1
		glXSwapIntervalEXTProc glXSwapIntervalEXT = 0;
		glXSwapIntervalEXT = (glXSwapIntervalEXTProc) glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
		glXSwapIntervalEXT(xwin.dpy, xwin.win, 1);

		// Init and verify GLEW
		glewExperimental = GL_TRUE;
		if(glewInit() != GLEW_OK){
			printf("Failed to init GLEW\n");
			// clean up ?
		}

		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );

		// TODO: X window v-synch

		// Create and bind global VAO object
		glGenVertexArrays(1, &tw_vao);
    	glBindVertexArray(tw_vao);

		timewarpShaderProgram = init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL);
		// Acquire attribute and uniform locations from the compiled and linked shader program



    	distortion_pos_attr = glGetAttribLocation(timewarpShaderProgram, "vertexPosition");
    	distortion_uv0_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv0");
    	distortion_uv1_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv1");
    	distortion_uv2_attr = glGetAttribLocation(timewarpShaderProgram, "vertexUv2");
    	tw_start_transform_unif = glGetUniformLocation(timewarpShaderProgram, "TimeWarpStartTransform");
    	tw_end_transform_unif = glGetUniformLocation(timewarpShaderProgram, "TimeWarpEndTransform");
    	tw_eye_index_unif = glGetUniformLocation(timewarpShaderProgram, "ArrayLayer");
    	eye_sampler_0 = glGetUniformLocation(timewarpShaderProgram, "Texture[0]");
    	eye_sampler_1 = glGetUniformLocation(timewarpShaderProgram, "Texture[1]");



		// Config distortion mesh position vbo
		glGenBuffers(1, &distortion_positions_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, distortion_positions_vbo);
		glBufferData(GL_ARRAY_BUFFER, HMD::NUM_EYES * (num_distortion_vertices * 3) * sizeof(GLfloat), distortion_positions, GL_STATIC_DRAW);
		glVertexAttribPointer(distortion_pos_attr, 3, GL_FLOAT, GL_FALSE, 0, 0);
		//glEnableVertexAttribArray(distortion_pos_attr);



		// Config distortion uv0 vbo
		glGenBuffers(1, &distortion_uv0_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, distortion_uv0_vbo);
		glBufferData(GL_ARRAY_BUFFER, HMD::NUM_EYES * (num_distortion_vertices * 2) * sizeof(GLfloat), distortion_uv0, GL_STATIC_DRAW);
		glVertexAttribPointer(distortion_uv0_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
		//glEnableVertexAttribArray(distortion_uv0_attr);


		// Config distortion uv1 vbo
		glGenBuffers(1, &distortion_uv1_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, distortion_uv1_vbo);
		glBufferData(GL_ARRAY_BUFFER, HMD::NUM_EYES * (num_distortion_vertices * 2) * sizeof(GLfloat), distortion_uv1, GL_STATIC_DRAW);
		glVertexAttribPointer(distortion_uv1_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
		//glEnableVertexAttribArray(distortion_uv1_attr);


		// Config distortion uv2 vbo
		glGenBuffers(1, &distortion_uv2_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, distortion_uv2_vbo);
		glBufferData(GL_ARRAY_BUFFER, HMD::NUM_EYES * (num_distortion_vertices * 2) * sizeof(GLfloat), distortion_uv2, GL_STATIC_DRAW);
		glVertexAttribPointer(distortion_uv2_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);
		//glEnableVertexAttribArray(distortion_uv2_attr);


		// Config distortion mesh indices vbo
		glGenBuffers(1, &distortion_indices_vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, distortion_indices_vbo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_distortion_indices * sizeof(GLuint), distortion_indices, GL_STATIC_DRAW);

		glXMakeCurrent(xwin.dpy, None, NULL);

		_m_thread = std::thread{&timewarp_gl::main_loop, this};
	}


	void GetViewMatrixFromPose( ksAlgebra::ksMatrix4x4f* viewMatrix, const pose_type& pose) {
		// Cast from the "standard" quaternion to our own, proprietary, Oculus-flavored quaternion
		auto latest_quat = ksAlgebra::ksQuatf {
			.x = pose.orientation.x(),
			.y = pose.orientation.y(),
			.z = pose.orientation.z(),
			.w = pose.orientation.w()
		};
		ksAlgebra::ksMatrix4x4f_CreateFromQuaternion( viewMatrix, &latest_quat);
	}

	virtual void warp(float time) {
		glXMakeCurrent(xwin.dpy, xwin.win, xwin.glc);

		auto most_recent_frame = _m_eyebuffer->get_latest_ro();
		if(!most_recent_frame){
			//std::cerr << "ATW failed to grab most recent frame from Switchboard" << std::endl;
			return;
		}

		glBindFramebuffer(GL_FRAMEBUFFER,0);
		glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		glClearColor(0, 0, 0, 0);
    	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glDepthFunc(GL_LEQUAL);


		// Use the timewarp program
		glUseProgram(timewarpShaderProgram);

		double warpStart = glfwGetTime();
		//double cursor_x, cursor_y;
		//glfwGetCursorPos(window, &cursor_x, &cursor_y);

		// Generate "starting" view matrix, from the pose
		// sampled at the time of rendering the frame.
		ksAlgebra::ksMatrix4x4f viewMatrix;
		GetViewMatrixFromPose(&viewMatrix, most_recent_frame->render_pose);

		// We simulate two asynchronous view matrices,
		// one at the beginning of display refresh,
		// and one at the end of display refresh.
		// The distortion shader will lerp between
		// these two predictive view transformations
		// as it renders across the horizontal view,
		// compensating for display panel refresh delay (wow!)
		ksAlgebra::ksMatrix4x4f viewMatrixBegin;
		ksAlgebra::ksMatrix4x4f viewMatrixEnd;

		// TODO: Right now, this samples the latest pose published to the "pose" topic.
		// However, this should really be polling the high-frequency pose prediction topic,
		// given a specified timestamp!
		auto latest_pose = _m_pose->get_latest_ro();
		GetViewMatrixFromPose(&viewMatrixBegin, *latest_pose);

		// std::cout << "Timewarp: old " << most_recent_frame->render_pose.pose << ", new " << latest_pose->pose << std::endl;

		// TODO: We set the "end" pose to the same as the beginning pose, because panel refresh is so tiny
		// and we don't need to visualize this right now (we also don't have prediction setup yet!)
		viewMatrixEnd = viewMatrixBegin;

		// Get HMD view matrices, one for the beginning of the
		// panel refresh, one for the end. (This is set to a 0.1s panel
		// refresh duration, for exaggerated effect)
		//GetHmdViewMatrixForTime(&viewMatrixBegin, glfwGetTime());
		//GetHmdViewMatrixForTime(&viewMatrixEnd, glfwGetTime() + 0.1f);

		//ksAlgebra::ksMatrix4x4f_CreateRotation( &viewMatrixBegin, (cursor_y - SCREEN_HEIGHT/2) * 0.05, (cursor_x - SCREEN_WIDTH/2) * 0.05, 0.0f );
		//ksAlgebra::ksMatrix4x4f_CreateRotation( &viewMatrixEnd, (cursor_y - SCREEN_HEIGHT/2) * 0.05, (cursor_x - SCREEN_WIDTH/2) * 0.05, 0.0f );

		// Calculate the timewarp transformation matrices.
		// These are a product of the last-known-good view matrix
		// and the predictive transforms.
		ksAlgebra::ksMatrix4x4f timeWarpStartTransform4x4;
		ksAlgebra::ksMatrix4x4f timeWarpEndTransform4x4;

		// DEMONSTRATION:
		// Every second, toggle timewarp on and off
		// to show the effect of the reprojection.
		/*
		if(glfwGetTime() < 9.0){
			viewMatrixBegin = viewMatrix;
			viewMatrixEnd = viewMatrix;
		}
		*/

		// Calculate timewarp transforms using predictive view transforms
		CalculateTimeWarpTransform(&timeWarpStartTransform4x4, &basicProjection, &viewMatrix, &viewMatrixBegin);
		CalculateTimeWarpTransform(&timeWarpEndTransform4x4, &basicProjection, &viewMatrix, &viewMatrixEnd);

		// We transform from 4x4 to 3x4 as we operate on vec3's in NDC space
		ksAlgebra::ksMatrix3x4f timeWarpStartTransform3x4;
		ksAlgebra::ksMatrix3x4f timeWarpEndTransform3x4;
		ksAlgebra::ksMatrix3x4f_CreateFromMatrix4x4f( &timeWarpStartTransform3x4, &timeWarpStartTransform4x4 );
		ksAlgebra::ksMatrix3x4f_CreateFromMatrix4x4f( &timeWarpEndTransform3x4, &timeWarpEndTransform4x4 );

		// Push timewarp transform matrices to timewarp shader
		glUniformMatrix3x4fv(tw_start_transform_unif, 1, GL_FALSE, (GLfloat*)&(timeWarpStartTransform3x4.m[0][0]));
		glUniformMatrix3x4fv(tw_end_transform_unif, 1, GL_FALSE,  (GLfloat*)&(timeWarpEndTransform3x4.m[0][0]));

		// Debugging aid, toggle switch for rendering in the fragment shader
		glUniform1i(glGetUniformLocation(timewarpShaderProgram, "ArrayIndex"), 0);

		glUniform1i(eye_sampler_0, 0);

		#ifndef USE_ALT_EYE_FORMAT
		// Bind the shared texture handle
		glBindTexture(GL_TEXTURE_2D_ARRAY, most_recent_frame->texture_handle);
		#endif

		glBindVertexArray(tw_vao);

		// Loop over each eye.
		for(int eye = 0; eye < HMD::NUM_EYES; eye++){

			#ifdef USE_ALT_EYE_FORMAT // If we're using Monado-style buffers we need to rebind eyebuffers.... eugh!
			glBindTexture(GL_TEXTURE_2D, most_recent_frame->texture_handles[eye]);
			#endif

			// The distortion_positions_vbo GPU buffer already contains
			// the distortion mesh for both eyes! They are contiguously
			// laid out in GPU memory. Therefore, on each eye render,
			// we set the attribute pointer to be offset by the full
			// eye's distortion mesh size, rendering the correct eye mesh
			// to that region of the screen. This prevents re-uploading
			// GPU data for each eye.
			glBindBuffer(GL_ARRAY_BUFFER, distortion_positions_vbo);
			glVertexAttribPointer(distortion_pos_attr, 3, GL_FLOAT, GL_FALSE, 0, (void*)(eye * num_distortion_vertices * sizeof(HMD::mesh_coord3d_t)));
			glEnableVertexAttribArray(distortion_pos_attr);

			// We do the exact same thing for the UV GPU memory.
			glBindBuffer(GL_ARRAY_BUFFER, distortion_uv0_vbo);
			glVertexAttribPointer(distortion_uv0_attr, 2, GL_FLOAT, GL_FALSE, 0, (void*)(eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
			glEnableVertexAttribArray(distortion_uv0_attr);

			// We do the exact same thing for the UV GPU memory.
			glBindBuffer(GL_ARRAY_BUFFER, distortion_uv1_vbo);
			glVertexAttribPointer(distortion_uv1_attr, 2, GL_FLOAT, GL_FALSE, 0, (void*)(eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
			glEnableVertexAttribArray(distortion_uv1_attr);

			// We do the exact same thing for the UV GPU memory.
			glBindBuffer(GL_ARRAY_BUFFER, distortion_uv2_vbo);
			glVertexAttribPointer(distortion_uv2_attr, 2, GL_FLOAT, GL_FALSE, 0, (void*)(eye * num_distortion_vertices * sizeof(HMD::mesh_coord2d_t)));
			glEnableVertexAttribArray(distortion_uv2_attr);


			#ifndef USE_ALT_EYE_FORMAT // If we are using normal ILLIXR-format eyebuffers
			// Specify which layer of the eye texture we're going to be using.
			// Each eye has its own layer.
			glUniform1i(tw_eye_index_unif, eye);
			#endif

			// Interestingly, the element index buffer is identical for both eyes, and is
			// reused for both eyes. Therefore glDrawElements can be immediately called,
			// with the UV and position buffers correctly offset.
			glDrawElements(GL_TRIANGLES, num_distortion_indices, GL_UNSIGNED_INT, (void*)0);
		}

		// Call Hologram
		auto hologram_params = new hologram_input;
		hologram_params->seq = ++_hologram_seq;
		_m_hologram->put(hologram_params);

		// Call swap buffers; when vsync is enabled, this will return to the CPU thread once the buffers have been successfully swapped.
		// TODO: GLX V SYNCH SWAP BUFFER
		glXSwapBuffers(xwin.dpy, xwin.win);
		lastSwapTime = glfwGetTime();
		averageFramerate = (RUNNING_AVG_ALPHA * (1.0 /(lastSwapTime - lastFrameTime))) + (1.0 - RUNNING_AVG_ALPHA) * averageFramerate;

		printf("\033[1;36m[TIMEWARP]\033[0m Motion-to-display latency: %.1f ms, Exponential Average FPS: %.3f\n", (float)(lastSwapTime - warpStart) * 1000.0f, (float)(averageFramerate));


		if(DISPLAY_REFRESH_RATE - averageFramerate > FPS_WARNING_TOLERANCE){
			printf("\033[1;36m[TIMEWARP]\033[0m \033[1;33m[WARNING]\033[0m Timewarp thread running slow!\n");
		}
		lastFrameTime = glfwGetTime();

	}

	virtual void _p_stop() override {
		_m_terminate.store(true);
		_m_thread.join();
	}

	virtual ~timewarp_gl() override {
		// TODO: Need to cleanup resources here!
		glXMakeCurrent(xwin.dpy, None, NULL);
 		glXDestroyContext(xwin.dpy, xwin.glc);
 		XDestroyWindow(xwin.dpy, xwin.win);
 		XCloseDisplay(xwin.dpy);
	}
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */

	// We sample the eyebuffer.
	#ifdef USE_ALT_EYE_FORMAT
	auto frame_ev = sb->subscribe_latest<rendered_frame_alt>("eyebuffer");
	#else
	auto frame_ev = sb->subscribe_latest<rendered_frame>("eyebuffer");
	#endif

	// We sample the up-to-date, predicted pose.
	auto pose_ev = sb->subscribe_latest<pose_type>("fast_pose");

	// We need global config data to create a shared GLFW context.
	auto config_ev = sb->subscribe_latest<global_config>("global_config");

	// We initiate hologram calls
	auto hologram_ev = sb->publish<hologram_input>("hologram_in");

	return new timewarp_gl {std::move(frame_ev), std::move(pose_ev), std::move(config_ev), std::move(hologram_ev)};
}
