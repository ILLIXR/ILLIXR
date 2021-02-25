#include <iostream>
#include <fstream>
#include <sstream>
#include "GL/gl.h"
#include <string>
#include <cstring>
#include <vector>
#include "global_module_defs.hpp"
#include "error_util.hpp"

using namespace ILLIXR;


static void GLAPIENTRY
	MessageCallback([[maybe_unused]] GLenum source,
					[[maybe_unused]] GLenum type,
					[[maybe_unused]] GLuint id,
					[[maybe_unused]] GLenum severity,
					[[maybe_unused]] GLsizei length,
					[[maybe_unused]] const GLchar* message,
					[[maybe_unused]] const void* userParam )
{
#ifndef NDEBUG
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return; // Don't show notification-severity messages.
    }
    std::cerr << "GL CALLBACK: " << (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "")
              << " type = 0x" << std::hex << type << std::dec
              << ", severity = 0x" << std::hex << severity << std::dec
              << ", message = " << message
              << std::endl;
    ILLIXR::abort();
#endif
}

static GLuint init_and_link (const char* vertex_shader, const char* fragment_shader){

    // GL handles for intermediary objects.
    GLint result, vertex_shader_handle, fragment_shader_handle, shader_program;

    vertex_shader_handle = glCreateShader(GL_VERTEX_SHADER);
    GLint vshader_len = strlen(vertex_shader);
    glShaderSource(vertex_shader_handle, 1, &vertex_shader, &vshader_len);
    glCompileShader(vertex_shader_handle);
    glGetShaderiv(vertex_shader_handle, GL_COMPILE_STATUS, &result);
    if ( result == GL_FALSE ) {
        GLchar msg[4096];
        GLsizei length;
        glGetShaderInfoLog( vertex_shader_handle, sizeof( msg ), &length, msg );
        std::cerr << "1 Error: " << msg << std::endl;
        std::exit(1);
    }

    GLint fragResult = GL_FALSE;
    fragment_shader_handle = glCreateShader(GL_FRAGMENT_SHADER);
    GLint fshader_len = strlen(fragment_shader);
    glShaderSource(fragment_shader_handle, 1, &fragment_shader, &fshader_len);
    glCompileShader(fragment_shader_handle);
    if (glGetError()) {
        std::cerr << "Fragment shader compilation failed" << std::endl;
        std::exit(1);
    }
    glGetShaderiv(fragment_shader_handle, GL_COMPILE_STATUS, &fragResult);
    if ( fragResult == GL_FALSE )
    {
        GLchar msg[4096];
        GLsizei length;
        glGetShaderInfoLog( fragment_shader_handle, sizeof( msg ), &length, msg );
        std::cerr << "2 Error: " << msg << std::endl;
        std::exit(1);
    }

    // Create program and link shaders
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader_handle);
    glAttachShader(shader_program, fragment_shader_handle);
    if (glGetError()) {
        std::cerr << "AttachShader or createProgram failed" << std::endl;
        std::exit(1);
    }

    ///////////////////
    // Link and verify

    glLinkProgram(shader_program);

    if(glGetError()){
        std::cerr << "Linking failed" << std::endl;
        std::exit(1);
    }

    glGetProgramiv(shader_program, GL_LINK_STATUS, &result);
    GLenum err = glGetError();
    if(err){
        std::cerr << "initGL, error getting link status, " << std::hex << err << std::dec << std::endl;
        std::exit(1);
    }
    if ( result == GL_FALSE )
    {
        GLsizei length = 0;

	    std::vector<GLchar> infoLog(length);
	    glGetProgramInfoLog(shader_program, length, &length, &infoLog[0]);

        std::string error_msg(infoLog.begin(), infoLog.end());
		std::cout << error_msg;
    }

    if (glGetError()) {
        std::cerr << "initGL, error at end of initGL" << std::endl;
        std::exit(1);
    }

    // After successful link, detach shaders from shader program
    glDetachShader(shader_program, vertex_shader_handle);
    glDetachShader(shader_program, fragment_shader_handle);

    return shader_program;
}
