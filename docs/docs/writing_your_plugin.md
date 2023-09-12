# Writing Your Plugin

## Adding a New Plugin

To add a new plugin

1. create a new subdirectory in the `plugins` directory named for your plugin (no spaces)
2. put your code in this new subdirectory (additional subdirectories containing parts of your code are allowed)
3. create a CMakeLists.txt file in this new subdirectory. See the template below
4. add the plugin to the `profiles/plugins.yaml` file, the name must match the subdirectory you created; it should go in the `internal_plugins` entry

For the examples below is for a plugin called tracker, so just replace any instance of tracker with
the name of your plugin.

### Simple Example
```cmake
1   set(TRACKER_SOURCES plugin.cpp
2                       src/tracker.cpp
3                       src/tracker.hpp)
4
5   set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})
6
7   add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})
8
9   target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
10
11  target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
12
13  install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line # | Notes                                                                                                                                                                                                        |
|--------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-3    | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                       |
| 5      | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                  |
| 7      | Tell the system we are building a shared library with the name  ` PLUGIN_NAME ` from the specified source files.                                                                                             |
| 9      | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR_SOURCE_DIR/include` in this, as this is where plugin.hpp and other ILLIXR common headers are. |
| 11     | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                |
| 13     | Add the install directive. This should not need to change.                                                                                                                                                   |

### More Complex Example
In this example the plugin has external dependencies provided by OS repos, specifically glfw3, x11, glew, glu, opencv, and eigen3.
```cmake
1   set(TRACKER_SOURCES plugin.cpp
2                       src/tracker.cpp
3                       src/tracker.hpp)
4
5   set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})
6
7   find_package(glfw3 REQUIRED)
8
9   add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})
10
11  if(BUILD_OPENCV)
12      add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
13  endif()
14
15  target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
16  target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} dl pthread)
17  target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
18
19  install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line# | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                       |
|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-3   | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                                                                                                                                                                                                                                                      |
| 5     | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                                                                                                                                                                                                                                                 |
| 7     | Use the `find_package` directive to locate any required dependencies. This will automatically populate variables containing header path and library names associated with the dependency. `find_package` assumes that there is an appropriate .cmake config file for the dependency on your system. If not the `pkg_check_module` function will perform the same task, but for dependencies which have associated .pc files on your system. |
| 9     | Tell the system we are building a shared library with the name   ` PLUGIN_NAME `  from the specified source files.                                                                                                                                                                                                                                                                                                                          |
| 11-13 | OpenCV is a special case for a dependency. If your plugin requires OpenCV add these lines to your CMakeLists.txt file and do not use `find_package(OpenCV)`.                                                                                                                                                                                                                                                                                |
| 15    | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR _SOURCE_DIR` in this, as this is where plugin.hpp and other ILLIXR common headers are.                                                                                                                                                                                                                                       |
| 16    | Tell the system about any libraries this plugin needs to link against (usually those associated with dependencies).                                                                                                                                                                                                                                                                                                                         |
| 17    | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                                                                                                                                                                                                                                               |
| 19    | Add the install directive. This should not need to change.                                                                                                                                                                                                                                                                                                                                                                                  |

**Note:** Not all the dependencies were searched for by `find_package` in this example. This is because there is a set
of dependencies which are very common to many plugins and their `find_package` calls are in the main ILLIXR CMakeLists.txt
file and do not need to be searched for again. These packages are

- Glew
- Glu
- SQLite3
- X11
- Eigen3

### Very Complex Example
In this example the plugin has dependencies provided by OS repos, and a third party dependency provided by a git repo.
```cmake
1   set(TRACKER_SOURCES plugin.cpp
2                       src/tracker.cpp
3                       src/tracker.hpp)
4
5   set(PLUGIN_NAME plugin.tracker${ILLIXR_BUILD_SUFFIX})
6
7   find_package(glfw3 REQUIRED)
8
9   add_library(${PLUGIN_NAME} SHARED ${TRACKER_SOURCES})
10
11  get_external(Plotter)
12
13  add_dependencies(${PLUGIN_NAME} Plotter)
14
15  if(BUILD_OPENCV)
16      add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
17  endif()
18
19  target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS} ${Plotter_INCLUDE_DIRS})
20  target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} ${Plotter_LIBRARIES} dl pthread)
21  target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
22
23  install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
```

| Line# | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                       |
|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1-3   | Specify the source code files individually, we discourage using  ` GLOB `  or  ` GLOB_RECURSE ` to generate a list of files as these functions do not always notice when files change.                                                                                                                                                                                                                                                      |
| 5     | Put the plugin name into a variable (will also be the name of the library).                                                                                                                                                                                                                                                                                                                                                                 |
| 7     | Use the `find_package` directive to locate any required dependencies. This will automatically populate variables containing header path and library names associated with the dependency. `find_package` assumes that there is an appropriate .cmake config file for the dependency on your system. If not the `pkg_check_module` function will perform the same task, but for dependencies which have associated .pc files on your system. |
| 9     | Tell the system we are building a shared library with the name   ` PLUGIN_NAME `  from the specified source files.                                                                                                                                                                                                                                                                                                                          |
| 11    | Get the external project called Plotter.                                                                                                                                                                                                                                                                                                                                                                                                    |
| 13    | Add the external package as a build dependency, this ensures that this plugin won't be built until after the dependency is.                                                                                                                                                                                                                                                                                                                 |
| 15-17 | OpenCV is a special case for a dependency. If your plugin requires OpenCV add these lines to your CMakeLists.txt file and do not use   `find_package(OpenCV)` .                                                                                                                                                                                                                                                                             |
| 19    | Tell the system about any non-standard include paths the compiler needs to be aware of. Always include `ILLIXR _SOURCE_DIR` in this, as this is where plugin.hpp and other ILLIXR common headers are.                                                                                                                                                                                                                                       |
| 20    | Tell the system about any libraries this plugin needs to link against (usually those associated with dependencies).                                                                                                                                                                                                                                                                                                                         |
| 21    | Any compile options specific to this plugin. Usually this will be left as is.                                                                                                                                                                                                                                                                                                                                                               |
| 23    | Add the install directive. This should not need to change.                                                                                                                                                                                                                                                                                                                                                                                  |

Additionally, to build and install the Plotter dependency you will need to create a cmake file in the `cmake` directory
named `GetPlotter.cmake` (case matters, it must match the call to `get_external`) with the following content.
```cmake
1   find_package(Plotter QUIET)
2
3   if(Plotter_FOUND)
4       set(Plotter_VERSION "${Plotter_VERSION_MAJOR}")
5   else()
6       EXTERNALPROJECT_ADD(Plotter
7               GIT_REPOSITORY https://github.com/mygit/Plotter.git
8               GIT_TAG 4ff860838726a5e8ac0cbe59128c58a8f6143c6c
9               PREFIX ${CMAKE_BINARY_DIR}/_deps/plotter
10              CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
11              )
12      set(Plotter_EXTERNAL Yes)
13      set(Plotter_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
14      set(Plotter_LIBRARIES plotter;alt_plotter)
15endif()
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
For plugins that are external packages (e.g. Audio_Pipeline) you need only create a `GetX.cmake` file as above and add the
plugin name to the `external_plugins` list in `profiles/plugins.yaml`.

External plugins with external dependencies are a bit more work, but are straight forward. See how Audio Pipeline is handled.

## Tutorial

You can extend ILLIXR for your own purposes.
To add your own functionality via the plugin interface:

1.  Create a new directory for your plugin in one of these ways 
    1. in the plugins directory of the main ILLIXR tree, then follow [these instructions](#adding-a-new-plugin)
    2. anywhere for your new plugin so it can be pushed as a git repository, the follow [these instructions](#adding-a-new-plugin) focussing on [external plugins](#external-plugins)

1.  You must decide if your plugin should inherit the standardized [`threadloop`][12]
        or [`plugin`][13].

    -   If your plugin just needs to run one computation repeatedly, then your
            plugin class should extend [`threadloop`][12]. Your code goes in
            `_p_one_iteration`, which gets called in a hot loop. `threadloop`
            inherits from plugin, but adds threading functionality. If you don't
            use `_p_one_iteration`, inheriting from `threadloop` is superfluous;
            Inherit from plugin directly instead.

    -   If you need custom concurrency (more complicated than a loop), triggered
            concurrency (by events fired in other plugins), or no concurrency
            then your plugin class should extend [`plugin`][13]. Your code goes
            in the `start` method.

    - If you want to schedule data-driven work in either case, call
      [`sb->schedule(...)`][14].
			
    - If you spin your own threads, they **must** wait for
          `pb->lookup_impl<Stoplight>()->wait_for_ready()` the first time they
          run. This allows the start of all threads in ILLIXR to be
          synchronized.

    - They **must** be joined-or-disowned at-or-before
          `plugin::stop()`. This allows ILLIXR to shutdown cleanly.

1.  Write a file called `plugin.cpp` with this body, replacing every instance of `plugin_name`:

    <!--- language: lang-cpp -->

        /// A minimal/no-op ILLIXR plugin

        #include "illixr/phonebook.hpp"
        #include "illixr/plugin.hpp"
        #include "illixr/threadloop.hpp"
        #include <chrono>
        #include <thread>

        using namespace ILLIXR;

        /// Inherit from plugin if you don't need the threadloop
        /// Inherit from threadloop to provide a new thread to perform the task
        class basic_plugin : public threadloop {
        public:
            basic_plugin(std::string name_, phonebook* pb_)
                : threadloop{name_, pb_}
            {
                std::cout << "Constructing basic_plugin." << std::endl;
            }

            /// Note the virtual.
            virtual ~basic_plugin() override {
                std::cout << "Deconstructing basic_plugin." << std::endl;
            }

            /// For `threadloop` style plugins, do not override the start() method unless you know what you're doing!
            /// _p_one_iteration() is called in a thread created by threadloop::start()
            void _p_one_iteration() override {
                std::cout << "This goes to the log when `log` is set in the config." << std::endl;
                std::cerr << "This goes to the console." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }

        };

        /// This line makes the plugin importable by Spindle
        PLUGIN_MAIN(basic_plugin);

1.  At this point, you should be able to build your plugin with ILLIXR using -DUSE<YOUR_PLUGIN_NAME>=ON as a command line argument to cmake.
     See [Getting Started][10] for more details.


1.  Finally, run ILLIXR with your new plugin following the instructions in [Getting Started][10]:


1.  This is all that is required to be a plugin which can be loaded by Spindle in
        the ILLIXR runtime.
    Reading and writing from Phonebook and Switchboard is optional,
        but nearly every plugin does it.
    See `default_plugins.md` for more details.

    First, we can query the [`phonebook`][11] to get various services
        including [`switchboard`][14].
    Then we query [`switchboard`][14] for event-streams (topics).
    We will read `topic1`, write to `topic2`, and schedule computation on `topic 3`.
    See the API documentation for `phonebook` and `switchboard` for more details.

    <!--- language: lang-cpp -->

        #include "illixr/phonebook.hpp"
        #include "illixr/plugin.hpp"
        #include "illixr/threadloop.hpp"
    
        /* When datatypes have to be common across plugins
         *     (e.g. a phonebook service or switchboard topic),
         *      they are defined in this header,
         *      which is accessible to all plugins.
         */
        #include "common/data_format.hpp"
    
        class plugin_name : public threadloop {
        public:
            /* After the constructor, C++ permits a list of member-constructors.
             * We use uniform initialization (curly-braces) [1] instead of parens to
             *     avoid ambiguity [2].
             * We put the comma at the start of the line, so that lines can be copied around
             *     or deleted freely (except for the first).
             *
             * [1]: https://en.wikipedia.org/wiki/C%2B%2B11#Uniform_initialization
             * [2]: https://en.wikipedia.org/wiki/Most_vexing_parse
             */
            plugin_name(std::string name_, phonebook* pb_)
                : threadloop{name_, pb_}
                  /// Find the switchboard in phonebook
                , sb{pb->lookup_impl<switchboard>()}
                  /// Create a handle to a topic in switchboard for subscribing
                , topic1{sb->get_reader<topic1_type>("topic1")}
                  /// Create a handle to a topic in switchboard for publishing
                , topic2{sb->get_writer<topic2_type>("topic2")}
            {
                /// Read topic 1
                switchboard::ptr<const topic1_type> event1 = topic1.get_ro();
    
                /// Write to topic 2
                topic2.put(
                    topic2.allocate<topic2_type>(
                        arg_1, // topic2_type::topic2_type() arg_type_1
                        ...,   // ...
                        arg_k  // topic2_type::topic2_type() arg_type_k
                    )
                );
    
                /// Read topic 3 synchronously
                sb->schedule<topic3_type>(
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
    
            virtual void _p_one_iteration override() {
                std::cout << "Running" << std::endl;
                auto target = std::chrono::system_clock::now()
                            + std::chrono::milliseconds{10};
                reliable_sleep(target);
            }
    
        private:
            const std::shared_ptr<switchboard> sb;
            switchboard::reader<topic1_type> topic1;
            switchboard::writer<topic2> topic2;
        };
    
        /// This line makes the plugin importable by Spindle
        PLUGIN_MAIN(plugin_name);


[//]: # (- Internal -)

[10]:   getting_started.md
[11]:   api/html/classILLIXR_1_1phonebook.html
[12]:   api/html/classILLIXR_1_1threadloop.html
[13]:   api/html/classILLIXR_1_1plugin.html
[14]:   api/html/classILLIXR_1_1switchboard.html
[15]:   glossary.md#plugin
