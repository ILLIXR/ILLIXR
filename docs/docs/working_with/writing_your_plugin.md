# Writing Your Plugin

## Adding a New Plugin

To add a new plugin

1. create a new subdirectory in the `plugins` directory named for your plugin (no spaces)
2. put your code in this new subdirectory (additional subdirectories containing parts of your code are allowed)
3. create a CMakeLists.txt file in this new subdirectory. See the template below
4. add the plugin to the `profiles/plugins.yaml` file, the name must match the subdirectory you created; it should go in
   the `all_plugins` entry

For the examples below is for a plugin called tracker, so just replace any instance of tracker with
the name of your plugin.

### Simple Example

``` cmake linenums="1" 
set(TRACKER_SOURCES plugin.cpp
                    plugin.hpp
                    src/tracker.cpp
                    src/tracker.hpp)

set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line # | Notes                                                                                                                                                                                                        |
|--------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-4    | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                       |
| 6      | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                  |
| 8      | Tell the system we are building a shared library with the name  ` PLUGIN_NAME ` from the specified source files.                                                                                             |
| 10     | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR_SOURCE_DIR/include` in this, as this is where plugin.hpp and other ILLIXR common headers are. |
| 12     | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                |
| 14     | Add the install directive. This should not need to change.                                                                                                                                                   |

### More Complex Example

In this example the plugin has external dependencies provided by OS repos, specifically glfw3, x11, glew, glu, opencv,
and eigen3.

``` cmake linenums="1"
set(TRACKER_SOURCES plugin.cpp
                    plugin.hpp
                    src/tracker.cpp
                    src/tracker.hpp)

set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})

find_package(glfw3 REQUIRED)

add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} dl pthread)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line# | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                       |
|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-4   | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                                                                                                                                                                                                                                                      |
| 6     | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                                                                                                                                                                                                                                                 |
| 8     | Use the `find_package` directive to locate any required dependencies. This will automatically populate variables containing header path and library names associated with the dependency. `find_package` assumes that there is an appropriate .cmake config file for the dependency on your system. If not the `pkg_check_module` function will perform the same task, but for dependencies which have associated .pc files on your system. |
| 10    | Tell the system we are building a shared library with the name   ` PLUGIN_NAME `  from the specified source files.                                                                                                                                                                                                                                                                                                                          |
| 12    | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR _SOURCE_DIR` in this, as this is where plugin.hpp and other ILLIXR common headers are.                                                                                                                                                                                                                                       |
| 13    | Tell the system about any libraries this plugin needs to link against (usually those associated with dependencies).                                                                                                                                                                                                                                                                                                                         |
| 14    | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                                                                                                                                                                                                                                               |
| 16    | Add the install directive. This should not need to change.                                                                                                                                                                                                                                                                                                                                                                                  |

!!! note

    Not all the dependencies were searched for by `find_package` in this example. This is because there is a set
    of dependencies which are very common to many plugins and their `find_package` calls are in the main ILLIXR CMakeLists.txt
    file and do not need to be searched for again. These packages are

    - Glew
    - Glu
    - SQLite3
    - X11
    - Eigen3
    - OpenCV

### Very Complex Example

In this example the plugin has dependencies provided by OS repos, and a third party dependency provided by a git repo.

``` cmake linenums="1"
set(TRACKER_SOURCES plugin.cpp
                    src/tracker.cpp
                    src/tracker.hpp)

set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})

find_package(glfw3 REQUIRED)

add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})

get_external(Plotter)

add_dependencies(${PLUGIN_NAME} Plotter)

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS} ${Plotter_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} ${Plotter_LIBRARIES} dl pthread)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line# | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                       |
|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-3   | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                                                                                                                                                                                                                                                      |
| 5     | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                                                                                                                                                                                                                                                 |
| 7     | Use the `find_package` directive to locate any required dependencies. This will automatically populate variables containing header path and library names associated with the dependency. `find_package` assumes that there is an appropriate .cmake config file for the dependency on your system. If not the `pkg_check_module` function will perform the same task, but for dependencies which have associated .pc files on your system. |
| 9     | Tell the system we are building a shared library with the name   ` PLUGIN_NAME `  from the specified source files.                                                                                                                                                                                                                                                                                                                          |
| 11    | Get the external project called Plotter.                                                                                                                                                                                                                                                                                                                                                                                                    |
| 13    | Add the external package as a build dependency, this ensures that this plugin won't be built until after the dependency is.                                                                                                                                                                                                                                                                                                                 |
| 15    | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR _SOURCE_DIR` in this, as this is where plugin.hpp and other ILLIXR common headers are.                                                                                                                                                                                                                                       |
| 16    | Tell the system about any libraries this plugin needs to link against (usually those associated with dependencies).                                                                                                                                                                                                                                                                                                                         |
| 17    | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                                                                                                                                                                                                                                               |
| 19    | Add the install directive. This should not need to change.                                                                                                                                                                                                                                                                                                                                                                                  |

Additionally, to build and install the Plotter dependency you will need to create a cmake file in the `cmake` directory
named `GetPlotter.cmake` (case matters, it must match the call to `get_external`) with the following content.

``` cmake linenums="1"
find_package(Plotter QUIET)

if(Plotter_FOUND)
    set(Plotter_VERSION "${Plotter_VERSION_MAJOR}")
else()
    EXTERNALPROJECT_ADD(Plotter
            GIT_REPOSITORY https://github.com/mygit/Plotter.git
            GIT_TAG 4ff860838726a5e8ac0cbe59128c58a8f6143c6c
            PREFIX ${CMAKE_BINARY_DIR}/_deps/plotter
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
            )
    set(Plotter_EXTERNAL Yes)
    set(Plotter_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(Plotter_LIBRARIES plotter;alt_plotter)
endif()
```

| Line # | Notes                                                                                                                          |
|--------|--------------------------------------------------------------------------------------------------------------------------------|
| 1      | See if the package has been previously installed, quietly fail if not.                                                         |
| 4      | If it was found, just record the installed version for reporting.                                                              |
| 6-11   | Add the Plotter package as a project called `Plotter` (also case sensitive)                                                    |
| 7      | The git repo where the Plotter package is located.                                                                             |
| 8      | The git tag to use (can be a tag name or sha5 from a commit).                                                                  |
| 9      | The build directory for the package.                                                                                           |
| 10     | Any camke arguments to pass to the Plotter build. The ones specified here are required, but any others can be added.           |
| 12     | Denote that this is an external package (this is used for internal tracking).                                                  |
| 13     | Set where the Plotter include files will land. Usually this will not need to change.                                           |
| 14     | Set which libraries are built by the Plotter package. In this example `libplotter.so` and `libalt_plotter.so` are being built. |

### External Plugins

For plugins that are external packages (e.g. Audio_Pipeline) you need only create a `GetX.cmake` file as above and add
the
plugin name to the `all_plugins` list in `profiles/plugins.yaml`.

External plugins with external dependencies are a bit more work, but are straight forward. See how Audio Pipeline is
handled.

For a plugin which uses the mediapipe framework (e.g. hand_tracking) see [Mediapipe Based Plugins][16] for
additional information.

## Tutorial

You can extend ILLIXR for your own purposes.
To add your own functionality via the plugin interface:

1. Create a new directory for your plugin in one of these ways
    1. in the plugins directory of the main ILLIXR tree, then follow [these instructions](#adding-a-new-plugin)
    2. anywhere for your new plugin so it can be pushed as a git repository, the
       follow [these instructions](#adding-a-new-plugin) focussing on [external plugins](#external-plugins)

2. You must decide if your plugin should inherit the standardized [`threadloop`][12]
   or [`plugin`][13].

    - If your plugin just needs to run one computation repeatedly, then your
      plugin class should extend [`threadloop`][12]. Your code goes in
      `_p_one_iteration`, which gets called in a hot loop. `threadloop`
      inherits from plugin, but adds threading functionality. If you don't
      use `_p_one_iteration`, inheriting from `threadloop` is superfluous;
      Inherit from plugin directly instead.

    - If you need custom concurrency (more complicated than a loop), triggered
      concurrency (by events fired in other plugins), or no concurrency
      then your plugin class should extend [`plugin`][13]. Your code goes
      in the `start` method.

    - If you want to schedule data-driven work in either case, call
      [`sb->schedule(...)`][14].

    - If you spin your own threads, they **must** wait for
      `pb->lookup_impl<stoplight>()->wait_for_ready()` the first time they
      run. This allows the start of all threads in ILLIXR to be
      synchronized.

    - They **must** be joined-or-disowned at-or-before
      `plugin::stop()`. This allows ILLIXR to shut down cleanly.

3. Write a file called `plugin.hpp` with this body, replacing every instance of `basic_plugin`:

    ``` Cpp
    #pragma once
    // A minimal/no-op ILLIXR plugin

    #include "illixr/phonebook.hpp"
    #include "illixr/plugin.hpp"
    #include "illixr/threadloop.hpp"
   
    namespace ILLIXR {

    // Inherit from plugin if you don't need the threadloop
    // Inherit from threadloop to provide a new thread to perform the task
    class basic_plugin : public threadloop {
    public:
        basic_plugin(std::string name_, phonebook* pb_);

        ~basic_plugin() override;
    protected:
        // For `threadloop` style plugins, do not override the start() method unless you know what you're doing.
        // _p_one_iteration() is called in a thread created by threadloop::start()
        void        _p_one_iteration() override;
        skip_option _p_should_skip() override;

    };
    }
    ```

4. Write a file called `plugin.cpp` which contains the implementations of which of the above functions.

    ``` Cpp
    // A minimal/no-op ILLIXR plugin

    #include "plugin.hpp"

    #include <iostream>
    #include <chrono>
    #include <thread>

    using namespace ILLIXR;

     basic_plugin::basic_plugin(std::string name_, phonebook* pb_)
           : threadloop{name_, pb_} {
            std::cout << "Constructing basic_plugin." << std::endl;
     }

     basic_plugin::~basic_plugin() {
         std::cout << "Deconstructing basic_plugin." << std::endl;
     }

     void basic_plugin::_p_one_iteration() {
         std::cout << "This goes to the log when `log` is set in the config." << std::endl;
         std::cerr << "This goes to the console." << std::endl;
         std::this_thread::sleep_for(std::chrono::milliseconds{100});
     }

    threadloop::skip_option _p_should_skip() {
        if (<CONDITION WHEN _p_one_iteration SHOULD RUN>)
            return skip_option::run;
        return skip_option::skip_and_spin;
    }
   
    // This line makes the plugin importable by Spindle
    PLUGIN_MAIN(basic_plugin);
    ```

5. At this point, you should be able to build your plugin with ILLIXR using `#!CMake -DUSE<YOUR_PLUGIN_NAME>=ON` as a command line
   argument to cmake.
   See [Getting Started][10] for more details.

6. Finally, run ILLIXR with your new plugin following the instructions in [Getting Started][10]:

7. This is all that is required to be a plugin which can be loaded by Spindle in
   the ILLIXR runtime.
   Reading and writing from Phonebook and Switchboard is optional,
   but nearly every plugin does it.

    First, we can query the [`phonebook`][11] to get various services
    including [`switchboard`][14].
    Then we query [`switchboard`][14] for event-streams (topics).
    We will read `topic1`, write to `topic2`, and schedule computation on `topic 3`.
    See the API documentation for `phonebook` and `switchboard` for more details.

    **plugin.hpp**
    ``` Cpp
    #pragma once
   
    #include "illixr/phonebook.hpp"
    #include "illixr/threadloop.hpp"

    class plugin_name : public threadloop {
    public:
        plugin_name(std::string name_, phonebook* pb_);

    protected:
        void _p_one_iteration() override;
   
    private:
        const std::shared_ptr<switchboard> switchboard_;
        switchboard::reader<topic1_type> topic1_;
        switchboard::writer<topic2> topic2_;
    };
    ```
   
    **plugin.cpp**
    ``` Cpp
    #include "plugin.hpp"
   
    #include <iostream>
   
    /* After the constructor, C++ permits a list of member-constructors.
     * We use uniform initialization (curly-braces) [1] instead of parens to
     *     avoid ambiguity [2].
     * We put the comma at the start of the line, so that lines can be copied around
     *     or deleted freely (except for the first).
     *
     * [1]: https://en.wikipedia.org/wiki/C%2B%2B11#Uniform_initialization
     * [2]: https://en.wikipedia.org/wiki/Most_vexing_parse
     */
    plugin_name::plugin_name(std::string name_, phonebook* pb_)
       : threadloop{name_, pb_}
       , switchboard_{pb->lookup_impl<switchboard>()}             // Find the switchboard in phonebook
       , topic1_{switchboard_->get_reader<topic1_type>("topic1")}  // Create a handle to a topic in switchboard for subscribing
       , topic2_{switchboard_->get_writer<topic2_type>("topic2")}  // Create a handle to a topic in switchboard for publishing
        {
        // Read topic 1
        switchboard::ptr<const topic1_type> event1 = topic1_.get_ro();

        // Write to topic 2
        topic2_.put(
            topic2_.allocate<topic2_type>(
                arg_1, // topic2_type::topic2_type() arg_type_1
                ...,   // ...
                arg_k  // topic2_type::topic2_type() arg_type_k
            )
        );

        /// Read topic 3 synchronously
        switchboard_->schedule<topic3_type>(
            get_name(),
            "topic3",
            [&](switchboard::ptr<const topic3_type> event3, std::size_t) {
                /* This is a [lambda expression][1]
                 *
                 * [1]: https://en.cppreference.com/w/cpp/language/lambda
                 */
                std::cout << "Got a new event on topic3: " << event3 << std::endl;
                callback(event3);
            }
        );
    }

    void _p_one_iteration() {
        std::cout << "Running" << std::endl;
        auto target = std::chrono::system_clock::now()
                    + std::chrono::milliseconds{10};
        reliable_sleep(target);
    }

    // This line makes the plugin importable by Spindle
    PLUGIN_MAIN(plugin_name);
    ```

[//]: # (- Internal -)

[10]:   ../getting_started.md

[11]:   ../api/classILLIXR_1_1phonebook.md

[12]:   ../api/classILLIXR_1_1threadloop.md

[13]:   ../api/classILLIXR_1_1plugin.md

[14]:   ../api/classILLIXR_1_1switchboard.md

[15]:   ../glossary.md#plugin

[16]:   adding_mediapipe.md
