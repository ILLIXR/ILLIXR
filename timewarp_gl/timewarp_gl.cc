#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/common.hh"
#include "utils/shader_util.hh"
#include "utils/algebra.hh"
#include "utils/hmd.hh"
#include "shaders/basic_shader.hh"
#include "shaders/timewarp_shader.hh"

using namespace ILLIXR;

class timewarp_gl : public abstract_timewarp {

static GLFWwindow* initWindow(int width, int height, GLFWwindow* shared, bool visible)
{
	GLFWwindow* win;
	if(visible)
		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
	else 
		glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
    win = glfwCreateWindow(width, height, "ILLIXR", 0, shared);
	return win;	
}


public:
	timewarp_gl() {
		
	}

private:

	static constexpr int   SCREEN_WIDTH    = 448*2;
	static constexpr int   SCREEN_HEIGHT   = 320*2;

	static constexpr double DISPLAY_REFRESH_RATE = 60.0;
	static constexpr double FPS_WARNING_TOLERANCE = 0.5;
	static constexpr double DELAY_FRACTION = 0.5;

	static constexpr double RUNNING_AVG_ALPHA = 0.33; // 0.33 roughly means 3 sample history

	GLFWwindow* window;
	rendered_frame frame;
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

		// Accumulate the transforms.
		ksAlgebra::ksMatrix4x4f_Multiply( transform, &texCoordProjection, &inverseDeltaViewMatrix );
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
			double sleep_start = glfwGetTime();
			std::this_thread::sleep_for(std::chrono::duration<double>(EstimateTimeToSleep(DELAY_FRACTION)));
			warp(glfwGetTime());
		}
	}
	/* compatibility interface */
	
	virtual void init(rendered_frame frame_handle, GLFWwindow* shared_context, hmd_physical_info* hmd_stats) override {

		// Generate reference HMD and physical body dimensions
    	HMD::GetDefaultHmdInfo(SCREEN_WIDTH, SCREEN_HEIGHT, &hmd_info);
		HMD::GetDefaultBodyInfo(&body_info);

		if(hmd_stats != NULL){
			// Override generate reference stats with specified hmd_stats
			for(int i = 0; i < 11; i++)
				hmd_info.K[i] = hmd_stats->K[i];
			for(int i = 0; i < 4; i++)
				hmd_info.chromaticAberration[i] = hmd_stats->chromaticAberration[i];

			body_info.interpupillaryDistance = hmd_stats->ipd;
			hmd_info.displayPixelsWide = hmd_stats->displayPixelsWide;
			hmd_info.displayPixelsHigh = hmd_stats->displayPixelsHigh;
			hmd_info.visiblePixelsWide = hmd_stats->visiblePixelsWide;
			hmd_info.visiblePixelsHigh = hmd_stats->visiblePixelsHigh;
			hmd_info.lensSeparationInMeters = hmd_stats->lensSeparationInMeters;
			hmd_info.metersPerTanAngleAtCenter = hmd_stats->metersPerTanAngleAtCenter;
		}
		


    	// Construct timewarp meshes and other data
    	BuildTimewarp(&hmd_info);


		window = initWindow(SCREEN_WIDTH, SCREEN_HEIGHT, shared_context, true);
		glfwMakeContextCurrent(window);
		
		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );

		this->frame = frame_handle;

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
		
		glfwMakeContextCurrent(NULL);

		_m_thread = std::thread{&timewarp_gl::main_loop, this};
	}

	// PLACEHOLDER, REMOVE
	// Will sample poses from main thread!
	void GetHmdViewMatrixForTime( ksAlgebra::ksMatrix4x4f * viewMatrix, float time )
	{

		// FIXME: use double?
		const float offset = time * 6.0f;
		const float degrees = 10.0f;
		const float degreesX = sinf( offset ) * degrees;
		const float degreesY = cosf( offset ) * degrees;

		ksAlgebra::ksMatrix4x4f_CreateRotation( viewMatrix, degreesX, degreesY, 0.0f );
	}

	virtual void warp(float time) override {
		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);
		glBindFramebuffer(GL_FRAMEBUFFER,0);
		glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		glClearColor(0, 0, 0, 0);
    	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glDepthFunc(GL_LEQUAL);
		
		
		// Use the timewarp program
		glUseProgram(timewarpShaderProgram);

		double warpStart = glfwGetTime();
		double cursor_x, cursor_y;
		glfwGetCursorPos(window, &cursor_x, &cursor_y);

		// Identity viewMatrix, simulates
		// the rendered scene's view matrix.
		ksAlgebra::ksMatrix4x4f viewMatrix;
		ksAlgebra::ksMatrix4x4f_CreateIdentity(&viewMatrix);

		// We simulate two asynchronous view matrices,
		// one at the beginning of display refresh,
		// and one at the end of display refresh.
		// The distortion shader will lerp between
		// these two predictive view transformations
		// as it renders across the horizontal view,
		// compensating for display panel refresh delay (wow!)
		ksAlgebra::ksMatrix4x4f viewMatrixBegin;
		ksAlgebra::ksMatrix4x4f viewMatrixEnd;

		// Get HMD view matrices, one for the beginning of the
		// panel refresh, one for the end. (This is set to a 0.1s panel
		// refresh duration, for exaggerated effect)
		//GetHmdViewMatrixForTime(&viewMatrixBegin, glfwGetTime());
		//GetHmdViewMatrixForTime(&viewMatrixEnd, glfwGetTime() + 0.1f);

		ksAlgebra::ksMatrix4x4f_CreateRotation( &viewMatrixBegin, (cursor_y - SCREEN_HEIGHT/2) * 0.05, (cursor_x - SCREEN_WIDTH/2) * 0.05, 0.0f );
		ksAlgebra::ksMatrix4x4f_CreateRotation( &viewMatrixEnd, (cursor_y - SCREEN_HEIGHT/2) * 0.05, (cursor_x - SCREEN_WIDTH/2) * 0.05, 0.0f );

		// Calculate the timewarp transformation matrices.
		// These are a product of the last-known-good view matrix
		// and the predictive transforms.
		ksAlgebra::ksMatrix4x4f timeWarpStartTransform4x4;
		ksAlgebra::ksMatrix4x4f timeWarpEndTransform4x4;

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

		// Bind the shared texture handle
		glBindTexture(GL_TEXTURE_2D_ARRAY, frame.texture_handle);

		glBindVertexArray(tw_vao);

		// Loop over each eye.
		for(int eye = 0; eye < HMD::NUM_EYES; eye++){

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

			// Specify which layer of the eye texture we're going to be using.
			// Each eye has its own layer.
			glUniform1i(tw_eye_index_unif, eye);

			// Interestingly, the element index buffer is identical for both eyes, and is
			// reused for both eyes. Therefore glDrawElements can be immediately called,
			// with the UV and position buffers correctly offset.
			glDrawElements(GL_TRIANGLES, num_distortion_indices, GL_UNSIGNED_INT, (void*)0);
		}

		
		
		// Call swap buffers; when vsync is enabled, this will return to the CPU thread once the buffers have been successfully swapped.
		glfwSwapBuffers(window);
		lastSwapTime = glfwGetTime();
		averageFramerate = (RUNNING_AVG_ALPHA * (1.0 /(lastSwapTime - lastFrameTime))) + (1.0 - RUNNING_AVG_ALPHA) * averageFramerate;

		printf("\033[1;36m[TIMEWARP]\033[0m Motion-to-display latency: %.1f ms, Exponential Average FPS: %.3f\n", (float)(lastSwapTime - warpStart) * 1000.0f, (float)(averageFramerate));

		
		if(DISPLAY_REFRESH_RATE - averageFramerate > FPS_WARNING_TOLERANCE){
			printf("\033[1;36m[TIMEWARP]\033[0m \033[1;33m[WARNING]\033[0m Timewarp thread running slow!\n");
		}
		lastFrameTime = glfwGetTime();
		
	}

	virtual ~timewarp_gl() override {
		_m_terminate.store(true);
		_m_thread.join();
	}
};

ILLIXR_make_dynamic_factory(abstract_timewarp, timewarp_gl)
