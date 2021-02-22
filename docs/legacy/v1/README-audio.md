# Audio Pipeline

Part of [ILLIXR][10], the Illinios Extended Reality Benchmark Suite.
The audio pipeline is responsible for both recording and playing back spatialized audio for XR.


# Build

This version simplifies the build process to automate building of both libspatialaudio
    and audio pipeline itself.
If you have a old version of this module and updating to the new version doesn't build correctly,
    you may need to purge the old module and clone this new version again.

Build debug:

```sh
make
```

or

```sh
make solo.dbg
```

Build release:

```sh
make solo.opt
```

**If you are switching between builds, please do `make deepclean`**

Also note that release build (-O3) would show great performance improvement over debug build.


# Usage

    ./solo.dbg <number of 1024-sample-block to process> <optional: decode/encode>

1.  Number of blocks to process is a required parameter.
2.  Decode/encode specifies the different audio processing procedures to take on,
        which is specificially designed for profiling.
    No output would be generated.

If encode or decode is not specified, the code will do both encode and decode
    on preset input sound sample files and generate a spatialized output audio at `output.wav`.


## Example:

    ./solo.dbg 500 

This will generate a ~10 seconds spatialized audio output from two sound samples under `./sample/`

    ./solo.dbg 2000 encode

This will encode 2000 sample blocks of audio input into ambisonics format.


## Notes

The input and output are hardcoded to be 48000 sample rate, 16-bit PCM wav file.

Also if you want to hear the output sound, limit the process sample blocks so that
    the output is not longer than input!
Otherwise, garbage sound samples would be generated.


# Components


## libspatialaudio

Submodule libspatialaudio provides the backend library for
    Ambisonics encoding,
    decoding,
    rotation,
    zoom,
    and
    binauralizer (HRTF included).


## audio pipeline code

### sound.cpp 

Describes a sound source in the ambisonics sound-field,
    including the input file for the sound source and its position in the sound-field.

### audio.cpp

Encapsulate preset processing steps of
    sound source reading,
    encoding,
    rotating,
    zooming,
    and
    decoding.


# License

This code is available under the University of Illinois/NCSA Open Source License.
The sound samples provided in ./sample/ are available under the Creative Commons 0 license.


[//]: # (- Internal -)

[10]:   index.md
