#include <GL/gl.h>

#define GLSL_VERSION            "430 core"

static const char* const blocki_vertex_shader =
    "#version " GLSL_VERSION "\n"
    "in vec4 vertexPosition;\n"
    "in vec3 vertexNormal;"
    
    "uniform mat4 u_modelview;\n"
    "uniform mat4 u_projection;\n"
    "out mediump vec3 worldPos;\n"
    "out mediump vec3 norm;\n"
    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vertexPosition;\n"
    "    worldPos = vec3(vertexPosition.x, vertexPosition.y, vertexPosition.z);\n"
    "    norm = vertexNormal;"
    "}\n";

static const char* const blocki_fragment_shader =
    "#version " GLSL_VERSION "\n"
    "precision mediump float;\n"

    "uniform vec4 u_color;\n"
    "in mediump vec3 worldPos;\n"
    "in mediump vec3 norm;\n"
    "out lowp vec4 outcolor;\n"
    "void main() {\n"
    "    float dist = gl_FragCoord.z/gl_FragCoord.w;\n"
    "    outcolor = u_color + vec4(1.0, 1.0, 1.0,1.0) * clamp(dist* 0.03, 0., 1.) * 0.7f;"
    "    outcolor = outcolor * (0.9 + clamp(dot(norm, vec3(1., 0.7, 0.3)), 0., 1.) * 0.3);"
    "}\n";