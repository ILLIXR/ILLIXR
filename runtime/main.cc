#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/component.hh"
#include "switchboard_impl.hh"
#include "dynamic_lib.hh"

using namespace ILLIXR;

static constexpr int   EYE_TEXTURE_WIDTH   = 1024;
static constexpr int   EYE_TEXTURE_HEIGHT  = 1024;


// Temporary OpenGL-specific code for creating shared OpenGL context.
// May be superceded in the future by more modular, Vulkan-based resource management.

void GLAPIENTRY
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

static GLFWwindow* initWindow(int width, int height, GLFWwindow* shared, bool visible)
{
	GLFWwindow* win;
	if(visible)
		glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
	else 
		glfwWindowHint(GLFW_VISIBLE, GL_FALSE);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    win = glfwCreateWindow(width, height, "ILLIXR", 0, shared);
	return win;	
}

int main(int argc, char** argv) {
	/* TODO: use a config-file instead of cmd-line args. Config file
	   can be more complex and can be distributed more easily (checked
	   into git repository). */

	auto sb = create_switchboard();

	// Grab a writer object and declare that we're publishing to the "global_config" topic, used to provide
	// components with global-scope general configuration information.
	std::cout << "Main is publishing global config data to Switchboard" << std::endl;
	std::unique_ptr<writer<global_config>> _m_config = sb->publish<global_config>("global_config");

	// Initialize the GLFW library.
	if(!glfwInit()){
		printf("Failed to initialize glfw\n");		
		return 0;
	}

	// Create a hidden window so we can provide a shared context
	// to all the sub-components. (May not be necessary in the future.)
	GLFWwindow* headless_window = initWindow(256,256,0,false);


	glfwMakeContextCurrent(headless_window);
	
	// Init and verify GLEW
	glewExperimental = GL_TRUE;
	if(glewInit() != GLEW_OK){
		printf("Failed to init GLEW\n");
		glfwDestroyWindow(headless_window);
		glfwTerminate();		
		return -1;
	}

	glEnable              ( GL_DEBUG_OUTPUT );
	glDebugMessageCallback( MessageCallback, 0 );

	// Now that we have our shared GLFW context, publish a pointer to our context using Switchboard!
	auto config = new global_config;
	config->glfw_context = headless_window;
	_m_config->put(config);

	// I have to keep the dynamic libs in scope until the program is dead
	std::vector<dynamic_lib> libs;
	std::vector<std::unique_ptr<component>> components;
	for (int i = 1; i < argc; ++i) {
		auto lib = dynamic_lib::create(std::string_view{argv[i]});
		auto comp = std::unique_ptr<component>(lib.get<create_component_fn>("create_component")(sb.get()));
		comp->start();
		libs.push_back(std::move(lib));
		components.push_back(std::move(comp));
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(10*1000));

	for (auto&& comp : components) {
		comp->stop();
	}

	return 0;
}
