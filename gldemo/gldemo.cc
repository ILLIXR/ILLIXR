#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/common.hh"
#include <cmath>

using namespace ILLIXR;

class gldemo : public abstract_gldemo {
public:
	gldemo() {
		
	}
	void main_loop() {
		double lastTime = glfwGetTime();
		glfwMakeContextCurrent(hidden_window);
		while (!_m_terminate.load()) {
			using namespace std::chrono_literals;
			// This "app" is "very slow"!
			std::this_thread::sleep_for(100ms);

			glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);
			
			// Draw things to left eye.
			glBindTexture(GL_TEXTURE_2D_ARRAY, frame.texture_handle);
			glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, frame.texture_handle, 0, 0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glClearColor(sin(glfwGetTime() * 10.0f + 3.0f), sin(glfwGetTime() * 10.0f + 5.0f), sin(glfwGetTime() * 10.0f + 7.0f), 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			// Draw things to right eye.
			glBindTexture(GL_TEXTURE_2D_ARRAY, frame.texture_handle);
			glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, frame.texture_handle, 0, 1);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glClearColor(cos(glfwGetTime() * 10.0f + 3.0f), cos(glfwGetTime() * 10.0f + 5.0f), cos(glfwGetTime() * 10.0f + 7.0f), 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			printf("[GL DEMO APP] Submitting frame, frametime %f, FPS: %f\n", (float)(glfwGetTime() - lastTime),  (float)(1.0/(glfwGetTime() - lastTime)));
			lastTime = glfwGetTime();
			glFlush();
			
		}
		
		
	}
private:
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};
	GLFWwindow* hidden_window;
	rendered_frame frame;

	GLuint eyeTextureFBO;
	GLuint eyeTextureDepthTarget;

	static void GLAPIENTRY
	MessageCallback( GLenum source,
					GLenum type,
					GLuint id,
					GLenum severity,
					GLsizei length,
					const GLchar* message,
					const void* userParam )
	{
	fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
			( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
				type, severity, message );
	}

	void createFBO(rendered_frame frame_to_attach){
		// Create a framebuffer to draw some things to the eye texture
		glGenFramebuffers(1, &eyeTextureFBO);
		// Bind the FBO as the active framebuffer.
    	glBindFramebuffer(GL_FRAMEBUFFER, eyeTextureFBO);

		glGenRenderbuffers(1, &eyeTextureDepthTarget);
    	glBindRenderbuffer(GL_RENDERBUFFER, eyeTextureDepthTarget);
    	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 1024, 1024);
    	//glRenderbufferStorageMultisample(GL_RENDERBUFFER, fboSampleCount, GL_DEPTH_COMPONENT, EYE_TEXTURE_WIDTH, EYE_TEXTURE_HEIGHT);
    	glBindRenderbuffer(GL_RENDERBUFFER, 0);

		// Bind eyebuffer texture
		printf("About to bind eyebuffer texture, texture handle: %d\n", frame_to_attach.texture_handle);
		glBindTexture(GL_TEXTURE_2D_ARRAY, frame_to_attach.texture_handle);
		glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, frame_to_attach.texture_handle, 0, 0);
    	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

		// attach a renderbuffer to depth attachment point
    	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, eyeTextureDepthTarget);

		if(glGetError()){
        	printf("displayCB, error after creating fbo\n");
    	}

		// Unbind FBO.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

public:
	/* compatibility interface */
	virtual void init(rendered_frame frame_handle, GLFWwindow* shared_context) override {
		// Create a hidden window, as we're drawing the demo "offscreen"
		glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
		hidden_window = glfwCreateWindow(1024, 1024, "GL Demo App", 0, shared_context);

		if(hidden_window == NULL){
			printf("Whoa, what?");
		}

		glfwMakeContextCurrent(hidden_window);

		glEnable              ( GL_DEBUG_OUTPUT );
		glDebugMessageCallback( MessageCallback, 0 );
		
		// Init and verify GLEW
		if(glewInit()){
			printf("Failed to init GLEW\n");
			glfwDestroyWindow(hidden_window);
		}
		this->frame = frame_handle;

		// Initialize FBO and depth targets, attaching to the frame handle
		createFBO(this->frame);

		glfwMakeContextCurrent(NULL);

		_m_thread = std::thread{&gldemo::main_loop, this};

	}
	virtual ~gldemo() override {
		_m_terminate.store(true);
		_m_thread.join();
		/*
		  This developer is responsible for killing their processes
		  and deallocating their resources here.
		*/
	}
};

ILLIXR_make_dynamic_factory(abstract_gldemo, gldemo)
