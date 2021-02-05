#include <iostream>
#include <fstream>
#include <sstream>
#include "GL/gl.h"
#include <string>
#include <cstring>
#include <vector>

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
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return; // Don't show notification-severity messages.
    }
	fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
			( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
				type, severity, message );
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
    if ( result == GL_FALSE )
    {
        GLchar msg[4096];
        GLsizei length;
        glGetShaderInfoLog( vertex_shader_handle, sizeof( msg ), &length, msg );
        printf( "1 Error: %s\n", msg);
        abort();
    }

    GLint fragResult = GL_FALSE;
    fragment_shader_handle = glCreateShader(GL_FRAGMENT_SHADER);
    GLint fshader_len = strlen(fragment_shader);
    glShaderSource(fragment_shader_handle, 1, &fragment_shader, &fshader_len);
    glCompileShader(fragment_shader_handle);
    if(glGetError()){
        printf("Fragment shader compilation failed\n");
        abort();
    }
    glGetShaderiv(fragment_shader_handle, GL_COMPILE_STATUS, &fragResult);
    if ( fragResult == GL_FALSE )
    {
        GLchar msg[4096];
        GLsizei length;
        glGetShaderInfoLog( fragment_shader_handle, sizeof( msg ), &length, msg );
        printf( "2 Error: %s\n", msg);
        abort();
    }

    // Create program and link shaders
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader_handle);
    glAttachShader(shader_program, fragment_shader_handle);
    if(glGetError()){
        printf("AttachShader or createProgram failed\n");
        abort();
    }

    ///////////////////
    // Link and verify

    glLinkProgram(shader_program);

    if(glGetError()){
        printf("Linking failed\n");
    }

    glGetProgramiv(shader_program, GL_LINK_STATUS, &result);
    GLenum err = glGetError();
    if(err){
        printf("initGL, error getting link status, %x", err);
        abort();
    }
    if ( result == GL_FALSE )
    {
        GLsizei length = 0;

	    std::vector<GLchar> infoLog(length);
	    glGetProgramInfoLog(shader_program, length, &length, &infoLog[0]);

        std::string error_msg(infoLog.begin(), infoLog.end());
		std::cout << error_msg;
    }

    if(glGetError()){
        printf("initGL, error at end of initGL");
        abort();
    }

    // After successful link, detach shaders from shader program
    glDetachShader(shader_program, vertex_shader_handle);
    glDetachShader(shader_program, fragment_shader_handle);

    return shader_program;
}
