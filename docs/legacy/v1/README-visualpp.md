## Visual Post-Processing Pipeline

Part of [ILLIXR][10], the Illinios Extended Reality Benchmark Suite.
This repo contains code for
    lens distortion correction,
    chromatic aberration correction,
    and
    timewarp.

Some of the FBO intialization code is borrowed from Song Ho Ahn's excellent tutorial series.
The particular FBO code used is found at his [website][1].

The image loader is based on Morten Nobel-Jørgensen's [blog post][2].

## Compiling and Running

Compile on Linux with the included makefile. Run with `./fbo <input image>`

We provide three examples, `landscape.png`, `museum.png`, and `tundra.png`,
    but any PNG image should work.


[//]: # (- References -)

[1]:    http://www.songho.ca/opengl/gl_fbo.html
[2]:    https://blog.nobel-joergensen.com/2010/11/07/loading-a-png-as-texture-in-opengl-using-libpng

[//]: # (- Internal -)

[10]:   index.md
