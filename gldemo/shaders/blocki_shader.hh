#include <GL/gl.h>

#define GLSL_VERSION            "430 core"

static const char* const blocki_vertex_shader =
    "#version " GLSL_VERSION "\n"
    "in vec4 vertexPosition;\n"

    "uniform mat4 u_modelview;\n"
    "uniform mat4 u_projection;\n"

    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vertexPosition;\n"
    "}\n";

static const char* const blocki_fragment_shader =
"#version " GLSL_VERSION "\n"
"precision mediump float;\n"

"uniform vec4 u_logo_color1;\n"
"uniform vec4 u_logo_color2;\n"
"out lowp vec4 outcolor"
"void main() {\n"
"    float dist = (gl_FragCoord.z - 0.6) * 7.0;\n"
"    outcolor = mix(u_logo_color2, u_logo_color1, dist);\n"
"}\n";