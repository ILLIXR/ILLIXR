# Adding Mediapipe Based Plugins

There are several tools from [Mediapipe][1] which a may be of use to the ILLIXR project as a plugin. This page documents the process for converting these tools to plugins, follwoing the experience of converting the hand larndmark detection tool.

# Steps
1. [Clone the repo](#clone-the-repo)
2. [Find the root file](#find-the-root-file)
3. [Understanding bazel BUILD files](#understanding-bazel-build-files)
    - [Select Statements](#select-statements)
    - [Descriptors](#example-descriptors)
        - [Binary](#binary-executable)
        - [Library](#library-component)
        - [Protobuf](#protobuf-library-component)
        - [Pbtxt](#pbtxt-files)
        - [External Libraries](#external-library)
        - [Data Files](#individual-file)
    - [Parsing Dependencies](#parsing-the-dependencies)
        - [Local](#local)
        - [In another file](#in-another-file)
        - [External](#external)
4. [Find the necessary code files](#find-the-necessary-code-files)
    - [C++](#c)
    - [Protobuf](#protobuf)
    - [Pbtxt](#pbtxt)
    - [Data](#data)
    - [Move the Files](#move-the-files)
5. [Package Dependencies](#package-dependencies)
    - [Common](#common)
    - [Custom](#custom)
6. [Plugin CMakeLists.txt](#plugin-cmakeliststxt)
    - [Header](#header)
    - [Command Line Options](#command-line-options)
    - [CMAKE_MODULE_PATH](#cmake_module_path)
    - [Dependencies](#dependencies)
    - [Protobuf Helpers](#protobuf-helpers)
    - [Special Operations](#special-operations)
    - [The Plugin Itself](#the-plugin-itself)
7. [Adapting the Tool for ILLIXR](#adapting-the-tool-for-illixr)
    - [protobuf.cmake Files](#protobufcmake-files)
    - [build.cmake Files](#buildcmake-files)
    - [Handling pbtxt Files](#handling-pbtxt-files)
    - [CMakeLists.txt](#cmakeliststxt)
8. [Writing Your Plugin](#writing-your-plugin)
    - [Understanding Graph Files](#understanding-graph-files)
    - [Sending Data to the Tool](#sending-data-to-the-tool)
    - [Getting Data From the Tool](#getting-data-from-the-tool)
        - [Adding a Calculator](#adding-a-calculator)
            - [Changing Graph Files](#changing-graph-files)
            - [Data Structures](#data-structures)
            - [Calculator Code](#calculator-code)
                - [GetContract](#getcontract)
                - [Open](#open)
                - [Process](#process)
    - [plugin.hpp](#pluginhpp)
    - [plugin.cpp](#plugincpp)

## Clone the repo

These tools are provided by Mediapipe in a single git repo (https://github.com/google-ai-edge/mediapipe.git). So clone the repo to your workspace. Since these tools share a number of dependencies and compnents in some cases it is best for all the wrapped tool to come from the same version of the Mediapipe codebase (currently v0.10.14).

<div class="code-box-copy">

    <button class="code-box-copy__btn" data-clipboard-target="#clone" title="Copy"></button>
    <pre class="language-shell" id="clone">git clone https://github.com/google-ai-edge/mediapipe.git
git checkout v0.10.14</pre>

</div>

or

<div class="code-box-copy">

    <button class="code-box-copy__btn" data-clipboard-target="#clone" title="Copy"></button>
    <pre class="language-shell" id="wget">wget https://github.com/google-ai-edge/mediapipe/archive/refs/tags/v0.10.14.tar.gz
tar xf v0.10.14.tar.gz
</div>

For purposes of this tutorial the term `file root` will refer to the root directory of the clone or unpacked tarball.

## Find the root file

Mediapipe tools use the bazel build system which is not compatible with the CMake build system ILLIXR uses. The bazel build system relies on files named BUILD and WORKSPACE as the primary building blocks to define what is to be built. You will need to locate the BUILD file associated with the tool you want to work with. These are located in mediapipe/example/desktop/<tool name>. For example the hand tracking BUILD file is mediapipe/examples/desktop/hand_tracking/BUILD

## Understanding bazel BUILD files

Bazel BUILD (and WORKSPACE) files are written in a dialect of Python called [Starlark][2]. Most of the content of these files describe libraries and binaries which can be built, similar to a [TARGET][3] in Cmake. Unlike CMake where most TARGETS are complete libraries or binaries with numerous source files, libraries and binaries in bazel typically have a few, or only one source file. Each of these items also has a list of other items that it depends on. Thus, starting with the main binary descriptor, you can walk your way through each of the dependencies to find all source files that are needed. A bazel project may consist of hundreds, or even thousands, of individual libraries, described in dozens of files scattered around the source tree.

There are only a few object types we need to concern ourselves with for wrapping a tool.

  - **cc_binary**: describes what is needed to build an executable
  - **cc_library**: describes what is needed to build a library component (usually produces a single object file)
  - **cc_library_with_tflite**: specialized version of cc_library which links to tensorflow lite components
  - **mediapipe_proto_library**: describes what is needed to build a library component from a protobuf file
  - **http_archive**: describes where to find and build an external library, similar to CMake's ExternalProject or Fetchcontent funtionality
  - **mediapipe_simple_subgraph**: describes a workflow, or sub-workflow, for a tool
  - **http_file**: describes where to find a single file on the web, typically a precompiled data file

### Select Statements

In the bazel build system the keyword `select` is used to denote an if/then/else syntax when constructing a list of source files or dependencies. The syntax is similar to a python dictionary where each key is a condition to test, and the value is a list of descriptors to include in whatever list the select statement is attached to. The `//conditions:default` is used to indicate what to use if none of the other options are true. For example

```python
deps = [
   "inference_calculator_cpu"
] + select({
   "//conditions:default": ["infer_calc_gl", "infer_shader"],
   ":platform_apple": ["infer_calc_metal"],
})
```

This statement would add the infer_calc_metal descriptor to the deps list if the build platform is apple, otherwise the infer_calc_gl and infer_shader are added. When these statements are encountered you will need to decide which descriptors need to be added to the appropriate list. In some instances you may need to set up similar logic in CMake syntax (e.g. include certain files if OpenCV is detected).

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

Both the **src** and **hdrs** are optional, and often only one is present in a descriptor. The `alwayslink` and `visibility` items can be safely ignored.

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

#### Pbtxt Files
There are two signatures for this type of file.
```python
mediapipe_simple_subgraph(
    name = "hand_landmark_model_loader",
    graph = "hand_landmark_model_loader.pbtxt",
    register_as = "HandLandmarkModelLoader",
    deps = [
        "//mediapipe/calculators/core:constant_side_packet_calculator",
        "//mediapipe/calculators/tflite:tflite_model_calculator",
        "//mediapipe/calculators/util:local_file_contents_calculator",
        "//mediapipe/framework/tool:switch_container",
    ],
)
```

  - **name**: name of the component
  - **graph**: the name of the file that describes the graph
  - **register_as**: alias for the component name
  - **deps**: list of dependencies for this component

```python
mediapipe_binary_graph(
    name = "hand_tracking_desktop_live_binary_graph",
    graph = "hand_tracking_desktop_live.pbtxt",
    output_name = "hand_tracking_desktop_live.binarypb",
    deps = [":desktop_tflite_calculators"],
)
```

  - **name**: name of the component
  - **graph**: the name of the file that describes the graph
  - **iutput_name**: name of the output files after processing
  - **deps**: list of dependencies for this component

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

Local dependencies are those which are described in the same BUILD file in which they are referenced. For example, in the `cc_library` descriptor above, the first **dep** is local. It starts with a `:`, followed by a name 
```
:landmarks_to_render_data_calculator_cc_proto
```
This means that there is a descriptor in this file with the name `landmarks_to_render_data_calculator_cc_proto`, and it is a dependency of `landmarks_to_render_data_calculator`.

#### In another file

Most of the dependencies in the `cc_library` example are described in other BUILD files. These entries start with `//` and have the following format 
```
//<path to build file>:<name of the dependency in that file>
```
So in the second dependency in the `cc_library` example (`//mediapipe/framework:calculator_framework`), the descriptor is named `calculator_framework` and can be found in `mediapipe/framework/BUILD` (relative to the file root). The [Local](#local) dependency format is really a specialized version of this format, where the path to the build file is absent, indicating the current file.

#### External

The remaining dependencies come from external libraries, this is really a list of libraries to link against, but which are also built along with this code. The entries start with `@` followed by the name of the external package, followed by the library path and name. So, `@com_google_absl//absl/memory` refers to the absl memory library described by an `http_archive` object with the name `com_google_absl`. As we refactor the code, and the build structure these types of references will be converted to something like
```cmake
target_link_library(landmarks_to_render_data_calculator PUBLIC absl::memory)
```

## Find the necessary code files

Now that we have a handle on the basic structure of the files we will be going through, we can start to construct a list of the files we actually need to build our tool. Starting with the dependencies in the `cc_binary` desriptor we will need to locate each dependency, then find all of their dependencies, and so on until the only descriptors left have no internal dependencies (external ones are ok, as these libraries will be built beforehand and do not contribute source files). Note that some objects will be the dependency of many other objetcs.

### C++

Create a list of every **src** and **hdr** file found in each descriptor. Note that the descriptors only give the file name, you will need to add the path to each of these items, so we can keep track of them. 

### Protobuf

Create a seperate list of the source files of the `mediapipe_proto_library` items you need.

### Pbtxt

Create a list of all files listed in graph elements of `mediapipe_simple_subgraph` items you need. Additioanlly, keep a list of all the dependencies for each of these, including the dependencies of those dependencies.

### Data

Create a list of all data files that are listed in any desciptors. Thes are mainly tflite files.

### Move the Files

Now that you have a list of the necessary file to build the tool you should create a seperate GitHub repostiory for your plugin. Once that is done, create files and directories in the repo as follows:

```bash
<root directory>
├── CMakeLists.txt      # Main cmake file
├── cmake               # empty directory for cmake helper files
├── mediapipe           # empty directory to hold all the mediapipe files
├── plugin.cpp          # main plugin code file
└── plugin.hpp          # plugion header file
```

Copy/move all the needed files from your **src**, **hdr**, **protobuf**, **pbtxt**, and **data** file lists into the `mediapipe` directory, maintining their relative paths.
You should also create  CMakeLists.txt and protobuf.cmake files in the mediapipe directory. Additionally, you should copy [encoder.cmake][10], [make_pb_binary.cmake][11], and [protoc_generate_obj.cmake][12] from the ILLIXR hand tracking repository. These files contain helper functions for processing the protobuf files and should go in a directory called `cmake`.
Now you should have a directory structure something like this:

```bash
<root directory>
├── CMakeLists.txt      # Main cmake file
├── cmake
│   ├── encoder.cmake
│   ├── make_pb_binary.cmake
│   └── protoc_generate_obj.cmake
├── mediapipe           
│   ├── calculators
│   ├── example
│   ├── framework
│   ├── gpu
│   ├── graphs
│   ├── module
│   └── utils
├── plugin.cpp          # main plugin code file
└── plugin.hpp          # plugion header file

```
## Package Dependencies

### Common

Common dependencies are those that can be via OS package managers, like boost. There is generally no need to install these directly. We will just add them as `find_package` commands in the CMakeLists.txt file. There are however, exceptions to this rule. An example of this is the Protobuf package. The Mediapipe package requires a Protobuf version of at least 3.19, which may not be directly available on all OS versions. In this case, the dependency should be treated like a [Custom](#custom) dependency.

### Custom

Custom dependencies are those which are not readily available via OS package managers, or a specific version which cannot be installed this way. In these cases will add some code to the CMakeLists.txt file to make sure the dependency is met. The CMakeLists.txt should first do a version check (in case the needed version already exists on the system), and then download and install the needed version if required. Below is an example for the Protobuf package

```cmake
find_package(protobuf 3.19 QUIET CONFIG)
if(protobuf_FOUND)
   report_found(protobuf "${protobuf_VERSION}")
else()
   ExternalProject_Add(
           protobuf
           GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
           GIT_TAG v3.19.1
           GIT_PROGRESS TRUE
           PREFIX "${CMAKE_BINARY_DIR}/protobuf"
           DOWNLOAD_EXTRACT_TIMESTAMP TRUE
           CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_FLAGS="-fPIC"
           SOURCE_SUBDIR cmake
           GIT_SUBMODULES_RECURSE TRUE
   )
endif()
```

As with the Hand Tracking plugin, there will likely be a good number of external package dependencies. So that all of the `find_package` calls in the plugin work as expected, a seperate [repo][13] was created. This allows for the dependencies to be built and installed before the plugin tries to find its dependencies. You may need a similar mechanism for your tool (or even just use the hand tracking one)

## Plugin CMakeLists.txt

In this section we will use the adaption of the Hand Tracking tool as an example. This file will be presented in sections below.

### Header
```cmake
cmake_minimum_required(VERSION 3.17)
project(ILLIXR_hand_tracking)
set(CMAKE_VERBOSE_MAKEFILE True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}/lib/cmake")
set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")
```
Here the name of the plugin/project is declared along with some global variables.

### Command Line Options
```cmake
include(CMakeDependentOption)

option(HT_ENABLE_GPU "Whether to enable GPU based codes vs CPU based" OFF)
option(HT_ENABLE_GRAPH_PROFILER "Whether to enable the graph profiler" OFF)
cmake_dependent_option(HT_ENABLE_WEB_PROFILING "Whether to enable web profiling" ON HT_ENABLE_GRAPH_PROFILER OFF)

if(HT_ENABLE_GPU)
    add_definitions(-DMEDIAPIPE_DISABLE_GPU=0)
else()
    add_definitions(-DMEDIAPIPE_DISABLE_GPU=1)
endif()
add_compile_options(-Wno-deprecated-declarations)
if(HT_ENABLE_GRAPH_PROFILER)
    add_definitions(-DMEDIAPIPE_PROFILER_AVAILABLE=1)
    if(HT_ENABLE_WEB_PROFILING)
        add_definitions(-DMEDIAPIPE_WEB_PROFILING_ENABLED=1)
    endif()
endif()
```
Here any command line options/flags are declared, and if needed, compile definitions are set.

### CMAKE_MODULE_PATH
```cmake
set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH}
    ${CMAKE_INSTALL_PREFIX}/lib/cmake
)
```
This section tells CMake where to look first for cmake files for external packages.

### Dependencies
```cmake
find_package(PkgConfig)

if(ILLIXR_ROOT)
    set(ILLIXR_HDR_PATH ${ILLIXR_ROOT} CACHE PATH "Location of ILLIXR headers")
    #add_definitions(-DBUILD_ILLIXR)
else()
    message(FATAL_ERROR "ILLIXR_ROOT must be specified")
endif()

set(protobuf_MODULE_COMPATIBLE ON)
find_package(ZLIB REQUIRED)
find_package(Protobuf 3.19 REQUIRED CONFIG)
pkg_check_modules(glog REQUIRED libglog)
pkg_check_modules(egl REQUIRED egl)
pkg_check_modules(glesv2 REQUIRED glesv2)
find_package(Eigen3 REQUIRED)
find_package(OpenCV 4 REQUIRED)
set(ENABLE_OPENCV ON)

set(PROTOBUF_DESCRIPTORS "" CACHE INTERNAL "")
pkg_check_modules(cpuinfo REQUIRED libcpuinfo)

find_package(tfl-XNNPACK REQUIRED CONFIG)
find_package(pthreadpool REQUIRED CONFIG)
find_package(tensorflow-lite REQUIRED CONFIG)

if(tfl-XNNPACK_BINARY_DIR)
    add_link_options(-L${tfl-XNNPACK_BINARY_DIR})
endif()
```
Here we search for any dependencies.

### Protobuf Helpers
```cmake
include(mediapipe/protobuf.cmake)
include(cmake/encoder.cmake)
```
The protobuf files from Mediapipe need some special processing. These files define functions which properly handle this.

### Special Operations
```cmake
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/mediapipe/calculators/tensor)
foreach(ITEM ${PROTOBUF_DESCRIPTORS})
    get_filename_component(_FILE ${ITEM} NAME_WE)
    add_custom_target(${_FILE}_desc
                      ALL
                      COMMAND cat ${ITEM} >> ${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin_temp
                      DEPENDS encode_as_c_string ${ITEM}
    )
    list(APPEND FILE_DEPS ${_FILE}_desc)
endforeach()
add_custom_target(move_temp_bin ALL
                  COMMAND mv ${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin_temp ${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin
                  DEPENDS ${FILE_DEPS}
                  BYPRODUCTS ${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin
)

add_custom_target(encode_descriptor_sets ALL
                  COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib" ${CMAKE_BINARY_DIR}/encode_as_c_string
                  ${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin > ${CMAKE_BINARY_DIR}/mediapipe/calculators/tensor/inference_calculator_proto_descriptors.inc
                  DEPENDS encode_as_c_string move_temp_bin
                  BYPRODUCTS ${CMAKE_BINARY_DIR}/mediapipe/calculators/tensor/inference_calculator_proto_descriptors.inc
)

include(cmake/message_util.cmake)
add_custom_target(make_message_type
                  COMMAND message_type_util
                  --input_path=${CMAKE_BINARY_DIR}/inference_calculator_proto_transitive-transitive-descriptor-set.proto.bin --root_type_macro_output_path=${CMAKE_BINARY_DIR}/mediapipe/calculators/tensor/inference_calculator_options_lib_type_name.h
                  DEPENDS message_type_util
                  BYPRODUCTS ${CMAKE_BINARY_DIR}/mediapipe/calculators/tensor/inference_calculator_options_lib_type_name.h
)
```
One of the steps required files to be concatenated together. This code replicates that functionality.

### The Plugin Itself
```cmake
set(PLUGIN_NAME plugin.hand_tracking${ILLIXR_BUILD_SUFFIX})
add_library(${PLUGIN_NAME} SHARED plugin.cpp)

add_subdirectory(mediapipe)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      ${OpenCV_LIBRARIES}
                      absl::base
                      absl::flags_parse
                      fmt
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${ILLIXR_HDR_PATH}
                           ${OpenCV_INCLUDE_DIRS}
                           ${CMAKE_BINARY_DIR}
                           ${CMAKE_SOURCE_DIR}
)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```
The plugin itself is declared as a library. All the hand tracking code is inside the mediapipe subdirectory. 

## Adapting the Tool for ILLIXR

Now that you have the code in place we will put together the build files needed. 

1. In the mediapipe directory create files called `CMakeLists.txt` and `protobuf.cmake`
2. In each subdirectory that contains source or header files (or whose subdirectories contain such files) create a `build.cmake` file
3. In each subdirectory that contains a protobuf file (or whose subdirectories contain such files) create a `protobuf.cmake` file

The `build.cmake` files will list all source files needed to build the tool, while the `protobuf.cmake` files will be used to process the protobuf files.

### protobuf.cmake Files

There are three types of `protobuf.cmake` files. The first are those in directories which contain protobuf files, while the second are those whose subdirectories contain protobuf files, and the third is a combination of the previous two types. For the second type, all the lines in the file will be including `protobuf.cmake` files from subdirectories. For example

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/core/protobuf.cmake)
```

For the first type, they will have an include file at the top, and then a function call for each protobuf file in the directory.

```cmake
include(${CMAKE_SOURCE_DIR}/cmake/protoc_generate_obj.cmake)

protobuf_generate_obj(PROTO_PATH calculators/util OBJ_NAME annotation_overlay_calculator)
```

The `protobuf_generate_obj` function generates a CMake TARGET which can be used to link against when compiling the code The signature for the function is:

  - **PROTO_PATH** - the path to the protobuf file (not inculding the file name), relative to the mediapipe directory
  - **OBJ_NAME** - the name of the protobuf file (without the extension)
  - **DESCRIPTORS** - A flag that will append descriptor files to the master list in CACHE

In the above example the CMake TARGET will be named calculators.util.annotation_overlay_calculator_proto. ?????? DESCRIPTORS

### build.cmake Files

Like the `protobuf.cmake` files, the `build.cmake` files also have three types, one for directories that only contain source/header files, one that contains only other directories, and one that is a combination of the previous two. For the second type, each of the lines of the file will be including other `build.cmake` files from subdirectories. For example

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/core/build.cmake)
```

For the first type the file will add all the source files in the directory to the main target. 

```cmake
set(UTIL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/annotation_overlay_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/association_norm_rect_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/collection_has_min_size_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/detection_letterbox_removal_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/detections_to_rects_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/detections_to_render_data_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/filter_collection_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/illixr_output_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/labels_to_render_data_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/landmark_letterbox_removal_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/landmark_projection_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/landmarks_to_render_data_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/local_file_contents_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/non_max_suppression_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/rect_to_render_data_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/rect_transformation_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/thresholding_calculator.cc
    ${CMAKE_CURRENT_LIST_DIR}/world_landmark_projection_calculator.cc
)

set(UTIL_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/association_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/collection_has_min_size_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/detections_to_rects_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/filter_collection_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/landmarks_to_render_data_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/illixr_output_calculator.h
    ${CMAKE_CURRENT_LIST_DIR}/illixr_data.h
)

target_sources(${PLUGIN_NAME} PRIVATE
               ${UTIL_SOURCES}
               ${UTIL_HEADERS}
)
```

### Handling pbtxt Files

In the `graph` and `modules` directories the `build.cmake` files will call functions to compile the subgraph files into a binary format.

The main input pbtxt file will need to be installed.

Some pbtxt files carry information about where tflite files are installed. These will need to be modified to contain the proper installed location.

The rest will need to be compiled.

### CMakeLists.txt

Now we will bring it all together in the `CMakeLists.txt` file in the mediapipe directory.

## Writing Your Plugin

In this guide we will refer to the entire mediapipe code as the tool. The workflow of the tool is defined by [graph][14] files. The individual [calculators][15] communicate with each other via [packets][17].

### Understanding Graph Files

The graph files (those with a pbtxt suffix) are the real drivers of the tool's workflow. They define what calculators are run in what order, and what their inputs and outputs are. In mediapipe, calculators are roughly equivalent to ILLIXR plugins. There are several levels of these graph files. The topmost one will be read as a text file by the tool's initializing code, and lives in mediapipe/graph/<tool> and are usually small compared to others. They also will link to other graph files (also called calculators in this context). All the other pbtxt files will be compiled into the tool library.

Let's look at the hand tracking main graph file (hand_tracking_desktop_live.pbtxt) in snipets.

```
# CPU image. (ImageFrame)
input_stream: "input_video"
input_stream: "image_data"
# CPU image. (ImageFrame)
output_stream: "illixr_data"
```

The `input_stream` keyword defines what data will be sent to the tool as input. The `output_stream` keyword defines what data will be sent out of the tool. As far as I know, there is no limit on the number of each. The value of both of these keywords is a string value which names the stream, these streams will be referred to by these names in the C++ code. At this level, there is no typing of the data, just labelling the streams.

```
node {
  calculator: "HandLandmarkTrackingCpu"
  input_stream: "IMAGE:input_video"
  input_side_packet: "NUM_HANDS:num_hands"
  output_stream: "LANDMARKS:landmarks"
  output_stream: "HANDEDNESS:handedness"
  output_stream: "PALM_DETECTIONS:multi_palm_detections"
  output_stream: "HAND_ROIS_FROM_LANDMARKS:multi_hand_rects"
  output_stream: "HAND_ROIS_FROM_PALM_DETECTIONS:multi_palm_rects"
}
```

The `node` defines a single calculator/subgraph. The name of the calculator/subgraph is defined in the `calculator` entry. This will match a class which has already bee compiled in the code (case-sensitive).

As above, the `input_stream` and `output_stream` define the inputs and outputs to/from the calculator. Note that the names of the streams are different from above. They have a format of `NAME:stream_name`. The fisrt, all caps, name acts as a reference to the second name. When the streams are referenced in the C++ code, the name used will be the first (or as above, only name). Thus using the two name scheme allows you to change the actual stream (second name) in the graph files, without haveing to change the C++ code, which uses the first name reference.

For a more complete guide on the graph file format see this [guide][14]. You can also visualize and error check graphs with this hany online [tool][16]

### Sending Data To the Tool

Sending data to the tool is straight forward. Take the following code snippet as an example

```C++
auto img_ptr = absl::make_unique<mediapipe::ImageData>(image_data);
auto img_status = _graph.AddPacketToInputStream(kImageDataTag,
                                                mediapipe::Adopt(img_ptr.release()).At(
                                                       mediapipe::Timestamp(frame_timestamp_us)));

if (!img_status.ok())
    throw std::runtime_error(std::string(img_status.message()));
```

The first line creates a unique pointer which wraps the data we want to send to the tool. The second line stages the packet to the graph with a call to `AddPacketToInputStream`. The first argument is the name of the stream to add the packet to (will be one of the names from the graph file), the second argument is the data being sent (it is fully adopted by the stream) along with the current timestamp. Note that the timestamp values do not have to match any specific timing convention, but must increase with each call to add a packet to a specific stream. The last two lines check that there was no error in adding the packet, and throw an exception if there was.

### Getting Data From the Tool

Getting the data from the tool is also straight forward. Take the following code snippet as an example

```C++
if (_poller->Next(&_packet))
    auto &output_frame = _packet.Get<mediapipe::ILLIXR::illixr_ht_frame>();
    ...
else
    ...
```

The if statement checks to see if there is a packet available. The `Next` function returns a boolean (true if a packet is available), and puts the packet, if any, in the `_packet` variable. The following line extracts and casts the packet to the apropriate type. The following lines can use the data `output_frame` in this exmaple, as needed.

#### Adding a Calculator

In some instances you may want to manipulate the output stream from a tool to better suit your needs. The original version of the hand tracking tool only returned an image with the hand tracking results visually represented. For ILLIXR, we wanted to have the actiual hand tracking data available (points on each hand, what hand(s) were detected, etc.). So a calculator was added to the end of the tool which gathered the relevant data together into a single structure. On of the other calculators was also modified so that the output image was transparent, so that the visual representation could be used as an overlay. Here we will look at how to create a new calculator from scratch.

##### Changing Graph Files

The first thing to do is to add a new `node` to the appropriate graph file. The name you give your calculator is case-sensitive and must match the calculator class name in your code. Be sure to add whatever input and output streams you will need.

##### Data Structures

Since whatever data the tool originally produced did not meet your needs, you will need to create a new data type which encapsulates all that you want. This data structure will be the packet you get from the graph at the end, and does not need to be the same data that your plugin publishes to ILLIXR. The file for this should be put in `mediapipe/calculators/util`. The following file is for the hand tracking tool, in `illixr_data.h`.

```C++ 
#pragma once
#include <vector>

#include "mediapipe/framework/port/opencv_core_inc.h"
#include "illixr/hand_tracking_data.hpp"

namespace mediapipe::ILLIXR {
struct illixr_ht_frame {
    cv::Mat* image = nullptr;

    size_t image_id;
    ::ILLIXR::image::image_type type;
    ::ILLIXR::rect* left_palm = nullptr;
    ::ILLIXR::rect* right_palm = nullptr;
    ::ILLIXR::rect* left_hand = nullptr;
    ::ILLIXR::rect* right_hand = nullptr;

    float left_confidence = 0.;
    float right_confidence = 0.;
    ::ILLIXR::hand_points* left_hand_points = nullptr;
    ::ILLIXR::hand_points* right_hand_points = nullptr;

    ~illixr_ht_frame() {
        delete image;
        delete left_palm;
        delete right_palm;
        delete left_hand;
        delete right_hand;
        delete left_hand_points;
        delete right_hand_points;
    }
};
}
```
It is just a `struct` that holds the needed data.

If this struct will be different from what your plugin will publish to ILLIXR, you should also add a header to `include/illixr` in your ILLIXR code base which defines that data structure. One main reason for having two different data structures is if the one for the tool has members whose data type is only defined in the mediapipe code. We don't want any other part of ILLIXR to need to depend on these external data types, so defining one inside ILLIXR (even if it is nearly identical), will alleviate this issue.

You will also need to add a protobuf file for defining the options used by your calculator. This guide won't go into the details of this, as it is likely that you will just need a basic one.

```protobuf
syntax = "proto2";

package mediapipe;

import "mediapipe/framework/calculator.proto";

message MyCalculatorOptions {
   extend CalculatorOptions {
      optional MyCalculatorOptions ext = 123456789;
   }
}
```

The number after ext must be unique withing the tool. Just grep for `ext` in the proto files and look for the largest one and add something like 10 to get your number. This file will also go in `mediapipe/calculators/util`.

##### Calculator Code

###### GetContract

###### Open

###### Process

### plugin.hpp

### plugin.cpp

###

[1]: https://ai.google.dev/edge/mediapipe/solutions/guide
[2]: https://github.com/bazelbuild/starlark
[3]: https://cmake.org/cmake/help/book/mastering-cmake/chapter/Key%20Concepts.html#targets

[10]: https://github.com/ILLIXR/hand_tracking/blob/main/cmake/encoder.cmake
[11]: https://github.com/ILLIXR/hand_tracking/blob/main/cmake/make_pb_binary.cmake
[12]: https://github.com/ILLIXR/hand_tracking/blob/main/cmake/protoc_generate_obj.cmake
[13]: https://github.com/ILLIXR/hand_tracking_dependencies
[14]: https://ai.google.dev/edge/mediapipe/framework/framework_concepts/graphs.md
[15]: https://ai.google.dev/edge/mediapipe/framework/framework_concepts/calculators.md
[16]: https://viz.mediapipe.dev/
[17]: https://ai.google.dev/edge/mediapipe/framework/framework_concepts/packets.md