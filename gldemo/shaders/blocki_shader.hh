#include <GL/gl.h>

#define GLSL_VERSION            "430 core"

static const char* const blocki_vertex_shader =
    "#version " GLSL_VERSION "\n"
    "in vec4 vertexPosition;\n"
    
    "uniform mat4 u_modelview;\n"
    "uniform mat4 u_projection;\n"
    "out mediump vec3 worldPos;\n"
    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vertexPosition;\n"
    "    worldPos = vec3(vertexPosition.x, vertexPosition.y, vertexPosition.z);\n"
    "}\n";

static const char* const blocki_fragment_shader =
    "#version " GLSL_VERSION "\n"
    "precision mediump float;\n"

    //"uniform vec4 u_logo_color1;\n"
    //"uniform vec4 u_logo_color2;\n"
    "in mediump vec3 worldPos;\n"
    "out lowp vec4 outcolor;\n"
    "void main() {\n"
    "    float dist = ((gl_FragCoord.z - 0.991) * 100.0);\n"
    "    outcolor = vec4(dist, dist, dist, 1.0);\n"
    "    float sum = worldPos.x*1.5 + worldPos.y*1.5 + worldPos.z*1.5;\n"
    "    float chess = floor(sum);\n"
    "    chess = fract(chess * 0.5);\n"
    "    chess *= 2;\n"
    "    outcolor = mix(vec4(0.921, 0.305, 0., 1.), vec4(0.09, 0.243, 0.588, 1.0), chess);\n"
    "    outcolor = mix(mix(outcolor, vec4(0.921, 0.305, 0., 1.), 0.0), vec4(0.0,0.0,0.0,1.0), dist);"
    //"    outcolor = vec4(1.0, 1.0, 1.0, 1.0);"
    "}\n";