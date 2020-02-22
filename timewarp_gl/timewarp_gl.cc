#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/common.hh"
#include "utils/shader_util.hh"
#include "utils/algebra.h"
#include "utils/hmd.h"
#include "shaders/basic_shader.hh"

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
	GLFWwindow* window;
	rendered_frame frame;
	GLuint basicShaderProgram;
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

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

	// Distortion shaders and shader program handles
	GLuint tw_vertex_shader;
	GLuint tw_frag_shader;
	GLuint tw_shader_program;
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
	mesh_coord3d_t* distortion_positions;
	GLuint distortion_positions_vbo;
	GLuint* distortion_indices;
	GLuint distortion_indices_vbo;
	uv_coord_t* distortion_uv0;
	GLuint distortion_uv0_vbo;
	uv_coord_t* distortion_uv1;
	GLuint distortion_uv1_vbo;
	uv_coord_t* distortion_uv2;
	GLuint distortion_uv2_vbo;

	// Handles to the start and end timewarp
	// transform matrices (3x4 uniforms)
	GLuint tw_start_transform_unif;
	GLuint tw_end_transform_unif;
	// Basic perspective projection matrix
	ksMatrix4x4f basicProjection;


	

public:

	void main_loop() {
		while (!_m_terminate.load()) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(10ms);
			warp(glfwGetTime());
		}
	}
	/* compatibility interface */
	
	virtual void init(rendered_frame frame_handle, GLFWwindow* shared_context) override {
		window = initWindow(500, 500, shared_context, true);
		glfwMakeContextCurrent(window);
		
		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );
		this->frame = frame_handle;
		basicShaderProgram = init_and_link(basicVertexShader, basicFragmentShader);
		// Acquire attribute and uniform locations from the compiled and linked shader program
    	basic_pos_attr = glGetAttribLocation(basicShaderProgram, "vertexPosition");
    	basic_uv_attr = glGetAttribLocation(basicShaderProgram, "vertexUV");

		
		glGenVertexArrays(1, &basic_vao);
    	glBindVertexArray(basic_vao);

		// Config basic mesh position vbo
    	glGenBuffers(1, &basic_pos_vbo);
    	glBindBuffer(GL_ARRAY_BUFFER, basic_pos_vbo);
    	glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(GLfloat), plane_vertices, GL_STATIC_DRAW);
    	glVertexAttribPointer(basic_pos_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);

		// Config basic mesh uv vbo
    	glGenBuffers(1, &basic_uv_vbo);
    	glBindBuffer(GL_ARRAY_BUFFER, basic_uv_vbo);
    	glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(GLfloat), plane_uvs, GL_STATIC_DRAW);
    	glVertexAttribPointer(basic_uv_attr, 2, GL_FLOAT, GL_FALSE, 0, 0);

		// Config basic mesh indices vbo
    	glGenBuffers(1, &basic_indices_vbo);
    	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, basic_indices_vbo);
    	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(GLfloat), plane_indices, GL_STATIC_DRAW);
		
		glfwMakeContextCurrent(NULL);

		_m_thread = std::thread{&timewarp_gl::main_loop, this};
	}

	virtual void warp(float time) override {
		printf("Warping\n");
		glfwMakeContextCurrent(window);
		glBindFramebuffer(GL_FRAMEBUFFER,0);
		glViewport(0,0,500,500);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(basicShaderProgram);
		glBindTexture(GL_TEXTURE_2D_ARRAY, frame.texture_handle);
    	glBindVertexArray(basic_vao);
    	glEnableVertexAttribArray(basic_pos_attr);
    	glEnableVertexAttribArray(basic_uv_attr);

		// Draw basic plane to put something in the FBO for testing
    	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

		glfwSwapBuffers(window);
	}

	virtual ~timewarp_gl() override {
		_m_terminate.store(true);
		_m_thread.join();
	}
};

ILLIXR_make_dynamic_factory(abstract_timewarp, timewarp_gl)
