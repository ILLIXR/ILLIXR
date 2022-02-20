# Writing Your Plugin

## Adding a New Plugin (Common Case)

In the common case, you only need to define a `Makefile` with the line `include common/common.mk`
    and symlink common (`ln -s ../common common`).
The included recipe file provides the necessary targets and uses the compiler `$(CXX)`,
    which is defined based on the OS and environment variables.
The included `Makefile`:

-   Compiles `plugin.cpp` and any other `*.cpp` files into the plugin.

-   Will invoke a recompile of the target any time any `*.hpp` or `*.cpp` file changes.

-   Compiles with C++17.
    You can change this in your plugin by defining
        `STDCXX = ...` before the `include`.
    This change will not affect other plugins; just yours.

-   Accepts specifying libraries by appending to `LDFLAGS` and `CFLAGS`.
    For example:

    <!--- language: lang-makefile -->

        LDFLAGS := $(LDFLAGS) $(shell pkg-config --ldflags eigen3)
        CFLAGS  := $(CFLAGS) $(shell pkg-config --cflags eigen3)

    See the source for the other flags and variables that you can set.

Finally, place the path of your plugin directory in the `plugin_group` list
    for the configuration you would like to run (e.g. `ILLIXR/configs/native.yaml`).


## Adding a New Plugin (General Case)

Each plugin can have a completely independent build system, as long as:

-   It defines a `Makefile` with targets for `plugin.dbg.so`, `plugin.opt.so`, and `clean`.
    Inside this `Makefile`, one can defer to another build system.

-   Its compiler maintains _ABI compatibility_ with the compilers used in every other plugin.
    Using the same version of Clang or GCC on the same architecture is sufficient for this.

-   Its path is in the `plugin_group` list for the configuration you would like
        to run (e.g. `ILLIXR/configs/native.yaml`).


## Tutorial

You can extend ILLIXR for your own purposes.
To add your own functionality via the plugin interface:

1.  Create a new directory anywhere for your new plugin and set it up for ILLIXR.
    We recommend you also push this plugin to a git repository on Github/Gitlab if you want it
        as a part of upstream ILLIXR in the future.

    -   Create a `Makefile` with the following contents.
        See [Building ILLIXR][10] for more details and alternative setups.

        <!--- language: lang-makefile -->

            include common/common.mk

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

    - If you want to scheduled data-driven work in either case, call
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

        #include "common/phonebook.hpp"
        #include "common/plugin.hpp"
        #include "common/threadloop.hpp"
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
                std::cout << "Constructing basic_plugin" << std::endl;
            }

            /// Note the virtual.
            virtual ~basic_plugin() override {
                std::cout << "Deconstructing basic_plugin" << std::endl;
            }

            /// For `threadloop` style plugins, do not override the start() method unless you know what you're doing!
            /// _p_one_iteration() is called in a thread created by threadloop::start()
            void _p_one_iteration() override {
                std::cout << "This goes to the log" << std::endl;
                std::cerr << "This goes to the console" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }

        };

        /// This line makes the plugin importable by Spindle
        PLUGIN_MAIN(basic_plugin);

1.  At this point, you should be able to build your plugin with ILLIXR.
    Move to the ILLIXR repo and update `configs/native.yaml`.
    If the new plugin is the same type as one of the other components you will need to
        remove that component from the config before running the new component.
    For example, if the new component is a SLAM then the old SLAM needs to be removed from
        the config.
    See [Building ILLIXR][10] for more details on the config file.

    <!--- language: lang-yaml -->

        plugin_groups:
          - !include "rt_slam_plugins.yaml"
          - !include "core_plugins.yaml"
          - plugin_group:
             - path: /PATH/TO/NEW/PLUGIN
             - path: ground_truth_slam/
             - path: gldemo/
             - path: debugview/
   
        data:
          subpath: mav0
          relative_to:
          archive_path:
          download_url: 'http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip'
          demo_data: demo_data/
          loader:
            name: native
            # command: gdb -q --args %a
            profile: opt

1.  Finally, run ILLIXR with your new plugin with the following command:

    <!--- language: lang-shell -->

        ./runner.sh configs/native.yaml

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

        #include "common/phonebook.hpp"
        #include "common/plugin.hpp"
        #include "common/threadloop.hpp"
    
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

[10]:   building_illixr.md
[11]:   api/html/classILLIXR_1_1phonebook.html
[12]:   api/html/classILLIXR_1_1threadloop.html
[13]:   api/html/classILLIXR_1_1plugin.html
[14]:   api/html/classILLIXR_1_1switchboard.html
[15]:   glossary.md#plugin
