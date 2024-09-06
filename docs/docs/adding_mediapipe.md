# Adding Mediapipe Based Plugins

There are several tools from Mediapipe[1] which a may be of use to the ILLIXR project as a plugin. This page documents the process for converting these tools to plugins, follwoing the experience of converting the hand larndmark detection tool.

# Steps
1. [Clone the repo](#clone-the-repo)
2. [Find the root file](#find-the-root-file)
3. [Understanding bazel BUILD files](#understanding-bazel-build-files)
4. [Find the necessary code files](#find-the-necessary-code-files)
   - [C++](#c)
   - [Protobuf](#protobuf)
5. [Dependencies](#dependencies)
   - [Common](#common)
   - [Custom](#custom)
6. [Building your CMakeLists.txt](#building-your-cmakeliststxt)

## Clone the repo

These tools are provided by Mediapipe in a single git repo (https://github.com/google-ai-edge/mediapipe.git). So clone the repo to your workspace. Since these tools share a number of dependencies and compnents in some cases it is best for all the wrapped tool to come from the same version of the Mediapipe codebase (currently v0.10.14).

```bash
git clone https://github.com/google-ai-edge/mediapipe.git
git checkout v0.10.14
```

or

```bash
wget https://github.com/google-ai-edge/mediapipe/archive/refs/tags/v0.10.14.tar.gz
tar xf v0.10.14.tar.gz
```

For purposes of this tutorial the term `file root` will refer to the root directory of the clone or unpacked tarball.

## Find the root file

Mediapipe tools use the bazel build system which is not compatible with the CMake build system ILLIXR uses. The bazel build system relies on files named BUILD and WORKSPACE as the primary building blocks to define what is to be built. You will need to locate the BUILD file associated with the tool you want to work with. These are located in mediapipe/example/desktop/<tool name>. For example the hand tracking BUILD file is mediapipe/examples/desktop/hand_tracking/BUILD

## Understanding bazel BUILD files

Bazel BUILD (and WORKSPACE) files are written in a dialect of Python called Starlark[2]. Most of the content of these files describe libraries and binaries which can be built, similar to a TARGET[3] in Cmake. Unlike CMake where most TARGETS are complete libraries or binaries with numerous source files, libraries and binaries in bazel typically have a few, or only one source file. Each of these items also has a list of other items that it depends on. Thus, starting with the main binary descriptor, you can walk your way through each of the dependencies to find all source files that are needed. A bazel project may consist of hundreds, or even thousands, of individual libraries, described in dozens of files scattered around the source tree.

There are only a few object types we need to concern ourselves with for wrapping a tool.

  - **cc_binary**: describes what is needed to build an executable
  - **cc_library**: describes what is needed to build a library component (usually produces a single object file)
  - **cc_library_with_tflite**: specialized version of cc_library which links to tensorflow lite components
  - **mediapipe_proto_library**: describes what is needed to build a library component from a protobuf file
  - **http_archive**: describes where to find and build an external library, similar to CMake's ExternalProject or Fetchcontent funtionality
  - **mediapipe_simple_subgraph**: describes a workflow, or sub-workflow, for a tool
  - **http_file**: describes where to find a single file on the web, typically a precompiled data file

### Example Descriptors

#### Binary (executable)
```python
cc_binary(
    name = "hand_tracking_cpu",
    data = [
        "//mediapipe/modules/hand_landmark:hand_landmark_full.tflite",
        "//mediapipe/modules/palm_detection:palm_detection_full.tflite",
    ],
    deps = [
        "//mediapipe/examples/desktop:demo_run_graph_main",
        "//mediapipe/graphs/hand_tracking:desktop_tflite_calculators",
    ],
)
```
  - **name**: the name of the executable to be built
  - **data**: data files which are used by the executable; these files can be downloaded from the web, and in CMake we treat them like a source file
  - **deps**: dependencies that this binary has

Note that the cc_binary descriptor does not give any source files, all source files come from the dependency listings.

#### Library component
```python
cc_library(
   name = "landmarks_to_render_data_calculator",
   srcs = ["landmarks_to_render_data_calculator.cc"],
   hdrs = ["landmarks_to_render_data_calculator.h"],
   deps = [
      ":landmarks_to_render_data_calculator_cc_proto",
      "//mediapipe/framework:calculator_framework",
      "//mediapipe/framework:calculator_options_cc_proto",
      "//mediapipe/framework/formats:landmark_cc_proto",
      "//mediapipe/framework/formats:location_data_cc_proto",
      "//mediapipe/framework/port:ret_check",
      "//mediapipe/util:color_cc_proto",
      "//mediapipe/util:render_data_cc_proto",
      "@com_google_absl//absl/memory",
      "@com_google_absl//absl/strings",
   ],
   alwayslink = 1,
)
```

  - **name**: name of the library component (think of it like a TARGET in CMake)
  - **srcs**: list of source files for this component
  - **hdrs**: list of header files for this component
  - **deps**: list of dependencies for this component (think of them as libraries to link against)

Both the **src** and **hdrs** are optional, and often only one is present in a descriptor. The `alwayslink` item can be safely ignored.

#### Protobuf library component
```python
mediapipe_proto_library(
    name = "annotation_overlay_calculator_proto",
    srcs = ["annotation_overlay_calculator.proto"],
    deps = [
        "//mediapipe/framework:calculator_options_proto",
        "//mediapipe/framework:calculator_proto",
        "//mediapipe/util:color_proto",
    ],
)
```
 
  - **name**: name of the library component (think of it like a TARGET in CMake)
  - **srcs**: list of protobuf source files for this component
  - **deps**: list of dependencies for this component (think of them a libraries to link against)

#### External library
```python
# Load Zlib before initializing TensorFlow and the iOS build rules to guarantee
# that the target @zlib//:mini_zlib is available
http_archive(
    name = "zlib",
    build_file = "@//third_party:zlib.BUILD",
    sha256 = "b3a24de97a8fdbc835b9833169501030b8977031bcb54b3b3ac13740f846ab30",
    strip_prefix = "zlib-1.2.13",
    url = "http://zlib.net/fossils/zlib-1.2.13.tar.gz",
    patches = [
        "@//third_party:zlib.diff",
    ],
    patch_args = [
        "-p1",
    ],
)
```

  - **name**: name of the external package
  - **build_file**: the location of the build file for the package
  - **sha256**: hash to check the integrity of the downloaded file
  - **url**: the full url of the package sources
  - **patches**: any patch files to be applied
  - **patch_args**: any additional command line arguments to send to the patch command

#### Individual file
```python
http_file(
    name = "com_google_mediapipe_hand_landmark_full_tflite",
    sha256 = "11c272b891e1a99ab034208e23937a8008388cf11ed2a9d776ed3d01d0ba00e3",
    urls = ["https://storage.googleapis.com/mediapipe-assets/hand_landmark_full.tflite?generation=1661875760968579"],
)
```
  - **name**: name of the file description (for internal reference, is not necessarily the name of the actual file)
  - **sha256**: hash to check the integrity of the downloaded file
  - **urls**: list of urls where the file can be downloaded from

### Parsing the dependencies

As you may have noted, there are numerous formats that a **dep** entry can take. Let's take a look at them, so we can understand what they are saying.

#### Local

Local dependencies are those which are described in the same BUILD file in which they are referenced. For example, in the `cc_library` descriptor above, the first **dep** is local. It starts with a `:`, followed by a name (`:landmarks_to_render_data_calculator_cc_proto`) This means that there is a descriptor in this file with the name `landmarks_to_render_data_calculator_cc_proto`, and it is a dependency of `landmarks_to_render_data_calculator`.

#### In another file

Most of the dependencies in the `cc_library` example are described in other BUILD files. These entries start with `//` and have the following format `//<path to build file>:<name of the dependency in that file>`. So in the second dependency in the `cc_library` example (`//mediapipe/framework:calculator_framework`), the descriptor is named `calculator_framework` and can be found in `mediapipe/framework/BUILD` (relative to the file root). The [Local](#local) dependency format is really a specialized version of this format, where the path to the build file is absent, indicating the current file.

#### External

The remaining dependencies come from external libraries, this is really a list of libraries to link against, but which are also built along with this code. The entries start with `@` followed by the name of the external package, followed by the library path and name. So, `@com_google_absl//absl/memory` refers to the absl memory library described by an `http_archive` object with the name `com_google_absl`. As we refactor the code, and the build structure these types of references will be converted to something like`target_link_library(landmarks_to_render_data_calculator PUBLIC absl::memory)`.

## Find the necessary code files

Now that we have a handle on the basic structure of the files we will be going through, we can start to construct a list of the files we actually need to build our tool. Starting with the dependencies in the `cc_binary` desriptor we will need to locate each dependency, then find all of their dependencies, and so on until the only descriptors left have no internal dependencies (external ones are ok, as these libraries will be built beforehand and do not contribute source files). Note that some objects will be the dependency of many other objetcs.

### C++

Create a list of every **src** and **hdr** file found in each descriptor. Note that the descriptors only give the file name, you will need to add the path to each of these items, so we can keep track of them. 

### Protobuf

Create a seperate list of the source files of the `mediapipe_proto_library` items you need.

## Dependencies

### Common

### Custom

## Building your CMakeLists.txt




[1]: https://ai.google.dev/edge/mediapipe/solutions/guide
[2]: https://github.com/bazelbuild/starlark
[3]: https://cmake.org/cmake/help/book/mastering-cmake/chapter/Key%20Concepts.html#targets