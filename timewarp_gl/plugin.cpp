#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/extended_window.hpp"
#include "common/shader_util.hpp"
#include "utils/hmd.hpp"
#include "common/math_util.hpp"
#include "shaders/basic_shader.hpp"
#include "shaders/timewarp_shader.hpp"
#include "common/pose_prediction.hpp"
#include "common/global_module_defs.hpp"

using namespace ILLIXR;

typedef void (*glXSwapIntervalEXTProc)(Display *dpy, GLXDrawable drawable, int interval);

const record_header timewarp_gpu_record {"timewarp_gpu", {
	{"iteration_no", typeid(std::size_t)},
	{"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
	{"wall_time_stop" , typeid(std::chrono::high_resolution_clock::time_point)},
	{"gpu_time_duration", typeid(std::chrono::nanoseconds)},
}};

const record_header mtp_record {"mtp_record", {
	{"iteration_no", typeid(std::size_t)},
	{"vsync", typeid(std::chrono::high_resolution_clock::time_point)},
	{"imu_to_display", typeid(std::chrono::nanoseconds)},
	{"predict_to_display", typeid(std::chrono::nanoseconds)},
	{"render_to_display", typeid(std::chrono::nanoseconds)},
}};

std::string
getenv_or(std::string var, std::string default_) {
	if (std::getenv(var.c_str())) {
		return {std::getenv(var.c_str())};
	} else {
		return default_;
	}
}

class timewarp_gl : public threadloop {

public:
	// Public constructor, create_component passes Switchboard handles ("plugs")
	// to this constructor. In turn, the constructor fills in the private
	// references to the switchboard plugs, so the component can read the
	// data whenever it needs to.
	timewarp_gl(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, pp{pb->lookup_impl<pose_prediction>()}
		, xwin{pb->lookup_impl<xlib_gl_extended_window>()}
		, _m_eyebuffer{sb->get_reader<rendered_frame>("eyebuffer")}
		, _m_hologram{sb->get_writer<switchboard::event_wrapper<std::size_t>>("hologram_in")}
		, _m_vsync_estimate{sb->get_writer<switchboard::event_wrapper<time_type>>("vsync_estimate")}
		, _m_mtp{sb->get_writer<switchboard::event_wrapper<std::chrono::duration<double, std::nano>>>("mtp")}
		, _m_frame_age{sb->get_writer<switchboard::event_wrapper<std::chrono::duration<double, std::nano>>>("warp_frame_age")}
		, timewarp_gpu_logger{record_logger_}
		, mtp_logger{record_logger_}
		  // TODO: Use #198 to configure this. Delete getenv_or.
		  // This is useful for experiments which seek to evaluate the end-effect of timewarp vs no-timewarp.
		  // Timewarp poses a "second channel" by which pose data can correct the video stream,
		  // which results in a "multipath" between the pose and the video stream.
		  // In production systems, this is certainly a good thing, but it makes the system harder to analyze.
		, disable_warp{bool(std::stoi(getenv_or("ILLIXR_TIMEWARP_DISABLE", "0")))}
	{ }

private:
	const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<pose_prediction> pp;

	static constexpr int   SCREEN_WIDTH    = ILLIXR::FB_WIDTH;
	static constexpr int   SCREEN_HEIGHT   = ILLIXR::FB_HEIGHT;

	static constexpr double DISPLAY_REFRESH_RATE = 60.0;
	static constexpr double FPS_WARNING_TOLERANCE = 0.5;

	// Note: 0.9 works fine without hologram, but we need a larger safety net with hologram enabled
	static constexpr double DELAY_FRACTION = 0.8;

	static constexpr double RUNNING_AVG_ALPHA = 0.1;

	static constexpr std::chrono::nanoseconds vsync_period {std::size_t(NANO_SEC/DISPLAY_REFRESH_RATE)};

	const std::shared_ptr<xlib_gl_extended_window> xwin;
	rendered_frame frame;

	// Switchboard plug for application eye buffer.
	switchboard::reader<rendered_frame> _m_eyebuffer;

	// Switchboard plug for sending hologram calls
	switchboard::writer<switchboard::event_wrapper<std::size_t>> _m_hologram;

	// Switchboard plug for publishing vsync estimates
	switchboard::writer<switchboard::event_wrapper<time_type>> _m_vsync_estimate;

	// Switchboard plug for publishing MTP metrics
	switchboard::writer<switchboard::event_wrapper<std::chrono::duration<double, std::nano>>> _m_mtp;

	// Switchboard plug for publishing frame stale-ness metrics
	switchboard::writer<switchboard::event_wrapper<std::chrono::duration<double, std::nano>>> _m_frame_age;

	record_coalescer timewarp_gpu_logger;
	record_coalescer mtp_logger;

	GLuint timewarpShaderProgram;

	time_type lastSwapTime;

	HMD::hmd_info_t hmd_info;
	HMD::body_info_t body_info;

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
	Eigen::Matrix4f basicProjection;

	// Hologram call data
	std::size_t _hologram_seq{0};

	bool disable_warp;

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
		math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.1f, 0.0f );
		// This was just temporary.
		free(tw_mesh_base_ptr);

		return;
	}

	/* Calculate timewarm transform from projection matrix, view matrix, etc */
	void CalculateTimeWarpTransform( Eigen::Matrix4f& transform, const Eigen::Matrix4f& renderProjectionMatrix,
                                        const Eigen::Matrix4f& renderViewMatrix, const Eigen::Matrix4f& newViewMatrix )
	{
		// Eigen stores matrices internally in column-major order.
		// However, the (i,j) accessors are row-major (i.e, the first argument
		// is which row, and the second argument is which column.)
		Eigen::Matrix4f texCoordProjection;
		texCoordProjection <<  0.5f * renderProjectionMatrix(0,0),            0.0f,                                          0.5f * renderProjectionMatrix(0,2) - 0.5f, 0.0f ,
							   0.0f,                                          0.5f * renderProjectionMatrix(1,1),            0.5f * renderProjectionMatrix(1,2) - 0.5f, 0.0f ,
							   0.0f,                                          0.0f,                                         -1.0f,                                      0.0f ,
							   0.0f,                                          0.0f,                                          0.0f,                                      1.0f;

		// Calculate the delta between the view matrix used for rendering and
		// a more recent or predicted view matrix based on new sensor input.
		Eigen::Matrix4f inverseRenderViewMatrix = renderViewMatrix.inverse();

		Eigen::Matrix4f deltaViewMatrix = inverseRenderViewMatrix * newViewMatrix;

		deltaViewMatrix(0,3) = 0.0f;
		deltaViewMatrix(1,3) = 0.0f;
		deltaViewMatrix(2,3) = 0.0f;

		// Accumulate the transforms.
		transform = texCoordProjection * deltaViewMatrix;
	}

	// Get the estimated time of the next swap/next Vsync.
	// This is an estimate, used to wait until *just* before vsync.
	time_type GetNextSwapTimeEstimate() {
		return lastSwapTime + vsync_period;
	}

	// Get the estimated amount of time to put the CPU thread to sleep,
	// given a specified percentage of the total Vsync period to delay.
	std::chrono::duration<double, std::nano> EstimateTimeToSleep(double framePercentage){
		return (GetNextSwapTimeEstimate() - std::chrono::high_resolution_clock::now()) * framePercentage;
	}


public:

	virtual skip_option _p_should_skip() override {
		using namespace std::chrono_literals;
		// Sleep for approximately 90% of the time until the next vsync.
		// Scheduling granularity can't be assumed to be super accurate here,
		// so don't push your luck (i.e. don't wait too long....) Tradeoff with
		// MTP here. More you wait, closer to the display sync you sample the pose.

		// TODO: poll GLX window events
		std::this_thread::sleep_for(std::chrono::duration<double>(EstimateTimeToSleep(DELAY_FRACTION)));
		if(_m_eyebuffer.get_nullable()) {
			return skip_option::run;
		} else {
			// Null means system is nothing has been pushed yet
			// because not all components are initialized yet
			return skip_option::skip_and_yield;
		}
	}

	virtual void _p_one_iteration() override {
		warp(glfwGetTime());
	}

	virtual void _p_thread_setup() override {
		lastSwapTime = std::chrono::high_resolution_clock::now();

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
		glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);

		// set swap interval for 1
		glXSwapIntervalEXTProc glXSwapIntervalEXT = 0;
		glXSwapIntervalEXT = (glXSwapIntervalEXTProc) glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
		glXSwapIntervalEXT(xwin->dpy, xwin->win, 1);

		// Init and verify GLEW
		glewExperimental = GL_TRUE;
		if(glewInit() != GLEW_OK){
			printf("Failed to init GLEW\n");
			// clean up ?
			exit(0);
		}

		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );

		// TODO: X window v-synch

		// Create and bind global VAO object
		glGenVertexArrays(1, &tw_vao);
    	glBindVertexArray(tw_vao);

    	#ifdef USE_ALT_EYE_FORMAT
    	timewarpShaderProgram = init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL_Alternative);
    	#else
		timewarpShaderProgram = init_and_link(timeWarpChromaticVertexProgramGLSL, timeWarpChromaticFragmentProgramGLSL);
		#endif
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

		glXMakeCurrent(xwin->dpy, None, NULL);
	}

	virtual void warp([[maybe_unused]] float time) {
		glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);

		glBindFramebuffer(GL_FRAMEBUFFER,0);
		glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		glClearColor(0, 0, 0, 0);
    	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glDepthFunc(GL_LEQUAL);

		auto most_recent_frame = _m_eyebuffer.get();

		// Use the timewarp program
		glUseProgram(timewarpShaderProgram);

		//double cursor_x, cursor_y;
		//glfwGetCursorPos(window, &cursor_x, &cursor_y);

		// Generate "starting" view matrix, from the pose
		// sampled at the time of rendering the frame.
		Eigen::Matrix4f viewMatrix = Eigen::Matrix4f::Identity();
		viewMatrix.block(0,0,3,3) = most_recent_frame->render_pose.pose.orientation.toRotationMatrix();
		// math_util::view_from_quaternion(&viewMatrix, most_recent_frame->render_pose.pose.orientation);

		// We simulate two asynchronous view matrices,
		// one at the beginning of display refresh,
		// and one at the end of display refresh.
		// The distortion shader will lerp between
		// these two predictive view transformations
		// as it renders across the horizontal view,
		// compensating for display panel refresh delay (wow!)
		Eigen::Matrix4f viewMatrixBegin = Eigen::Matrix4f::Identity();
		Eigen::Matrix4f viewMatrixEnd = Eigen::Matrix4f::Identity();

		// TODO: Right now, this samples the latest pose published to the "pose" topic.
		// However, this should really be polling the high-frequency pose prediction topic,
		// given a specified timestamp!
		const fast_pose_type latest_pose = disable_warp ? most_recent_frame->render_pose : pp->get_fast_pose();
		viewMatrixBegin.block(0,0,3,3) = latest_pose.pose.orientation.toRotationMatrix();

		// TODO: We set the "end" pose to the same as the beginning pose, because panel refresh is so tiny
		// and we don't need to visualize this right now (we also don't have prediction setup yet!)
		viewMatrixEnd = viewMatrixBegin;

		// Calculate the timewarp transformation matrices.
		// These are a product of the last-known-good view matrix
		// and the predictive transforms.
		Eigen::Matrix4f timeWarpStartTransform4x4;
		Eigen::Matrix4f timeWarpEndTransform4x4;

		// Calculate timewarp transforms using predictive view transforms
		CalculateTimeWarpTransform(timeWarpStartTransform4x4, basicProjection, viewMatrix, viewMatrixBegin);
		CalculateTimeWarpTransform(timeWarpEndTransform4x4, basicProjection, viewMatrix, viewMatrixEnd);

		glUniformMatrix4fv(tw_start_transform_unif, 1, GL_FALSE, (GLfloat*)(timeWarpStartTransform4x4.data()));
		glUniformMatrix4fv(tw_end_transform_unif, 1, GL_FALSE,  (GLfloat*)(timeWarpEndTransform4x4.data()));

		// Debugging aid, toggle switch for rendering in the fragment shader
		glUniform1i(glGetUniformLocation(timewarpShaderProgram, "ArrayIndex"), 0);

		glUniform1i(eye_sampler_0, 0);

		#ifndef USE_ALT_EYE_FORMAT
		// Bind the shared texture handle
		glBindTexture(GL_TEXTURE_2D_ARRAY, most_recent_frame->texture_handle);
		#endif

		glBindVertexArray(tw_vao);

		auto gpu_start_wall_time = std::chrono::high_resolution_clock::now();

		GLuint query;
		GLuint64 elapsed_time = 0;
		glGenQueries(1, &query);
		glBeginQuery(GL_TIME_ELAPSED, query);

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

		glEndQuery(GL_TIME_ELAPSED);

#ifndef NDEBUG
		auto delta = std::chrono::high_resolution_clock::now() - most_recent_frame->render_time;
		if (log_count > LOG_PERIOD) {
			printf("\033[1;36m[TIMEWARP]\033[0m Time since render: %3fms\n", (float)(delta.count() / 1000000.0));
			// We have always been warping from the correct swap, so I will disable this.
			// printf("\033[1;36m[TIMEWARP]\033[0m Warping from swap %d\n", most_recent_frame->swap_indices[0]);
		}

		if(delta > vsync_period)
		{
			printf("\033[0;31m[TIMEWARP: CRITICAL]\033[0m Stale frame!\n");
		}
#endif
		// Call Hologram
		_m_hologram.put(new (_m_hologram.allocate()) switchboard::event_wrapper<std::size_t>{++_hologram_seq});

		// Call swap buffers; when vsync is enabled, this will return to the CPU thread once the buffers have been successfully swapped.
		// TODO: GLX V SYNCH SWAP BUFFER
		[[maybe_unused]] auto beforeSwap = glfwGetTime();

		glXSwapBuffers(xwin->dpy, xwin->win);

		// The swap time needs to be obtained and published as soon as possible
		lastSwapTime = std::chrono::high_resolution_clock::now();
		[[maybe_unused]] auto afterSwap = glfwGetTime();

		// Now that we have the most recent swap time, we can publish the new estimate.
		_m_vsync_estimate.put(new (_m_vsync_estimate.allocate()) switchboard::event_wrapper<time_type>{GetNextSwapTimeEstimate()});

		std::chrono::nanoseconds imu_to_display = lastSwapTime - latest_pose.pose.sensor_time;
		std::chrono::nanoseconds predict_to_display = lastSwapTime - latest_pose.predict_computed_time;
		std::chrono::nanoseconds render_to_display = lastSwapTime - most_recent_frame->render_time;

		mtp_logger.log(record{mtp_record, {
			{iteration_no},
			{lastSwapTime},
			{imu_to_display},
			{predict_to_display},
			{render_to_display},
		}});

#ifndef NDEBUG
		if (log_count > LOG_PERIOD) {
			printf("\033[1;36m[TIMEWARP]\033[0m Swap time: %5fms\n", (float)(afterSwap - beforeSwap) * 1000);
			printf("\033[1;36m[TIMEWARP]\033[0m Motion-to-display latency: %3f ms\n", float(imu_to_display.count()) / 1e6);
			printf("\033[1;36m[TIMEWARP]\033[0m Prediction-to-display latency: %3f ms\n", float(predict_to_display.count()) / 1e6);
			printf("\033[1;36m[TIMEWARP]\033[0m Render-to-display latency: %3f ms\n", float(render_to_display.count()) / 1e6);
			std::cout<< "Timewarp estimating: " << std::chrono::duration_cast<std::chrono::milliseconds>(GetNextSwapTimeEstimate() - lastSwapTime).count() << "ms in the future" << std::endl;
		}
#endif

		// retrieving the recorded elapsed time
		// wait until the query result is available
		int done = 0;
		glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &done);
		while (!done) {
			std::this_thread::yield();
			glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &done);
		}

		// get the query result
		glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed_time);
		timewarp_gpu_logger.log(record{timewarp_gpu_record, {
			{iteration_no},
			{gpu_start_wall_time},
			{std::chrono::high_resolution_clock::now()},
			{std::chrono::nanoseconds(elapsed_time)},
		}});

#ifndef NDEBUG
		if (log_count > LOG_PERIOD) {
			log_count = 0;
		} else {
			log_count++;
		}
#endif
	}

#ifndef NDEBUG
	size_t log_count = 0;
	size_t LOG_PERIOD = 20;
#endif

	virtual ~timewarp_gl() override {
		// TODO: Need to cleanup resources here!
		glXMakeCurrent(xwin->dpy, None, NULL);
 		glXDestroyContext(xwin->dpy, xwin->glc);
 		XDestroyWindow(xwin->dpy, xwin->win);
 		XCloseDisplay(xwin->dpy);
	}
};

PLUGIN_MAIN(timewarp_gl)
