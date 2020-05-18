#include <vector>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "common/plugin.hpp"
#include "common/data_format.hpp"
#include "dynamic_lib.hpp"
#include "phonebook_impl.hpp"
#include "switchboard_impl.hpp"

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

	auto pb = create_phonebook();
	auto sb = create_switchboard().release();
	
	pb->register_impl<switchboard>(sb);
	pb->register_impl<global_config>(new global_config{headless_window});

	// I have to keep the dynamic libs in scope until the program is dead
	// so I will add them to a vector
	std::vector<dynamic_lib> libs;
	std::vector<std::unique_ptr<plugin>> plugins;
	for (int i = 1; i < argc; ++i) {
		auto lib = dynamic_lib::create(std::string_view{argv[i]});
		plugin* p = lib.get<plugin* (*) (phonebook*)>("plugin_main")(pb.get());
		plugins.emplace_back(p);
		libs.push_back(std::move(lib));
	}

	while (true) { }

	return 0;
}
