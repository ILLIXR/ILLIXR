# Writing your plugin

## Adding a new plugin (common case)

In the common case, one need only define a `Makefile` with the line `include common/common.mk` and
symlink common (`ln -s ../common common`). This provides the necessary targets and uses the compiler
`$(CXX)`, which is defined in Make based on the OS and environment variables.

- It compiles `plugin.cpp` and any other `*.cpp` files into the plugin.

- It will invoke a recompile the target any time any `*.hpp` or `*.cpp` file changes.

- It compiles with C++17. You can change this in your plugin by defining `STDCXX = ...` before the
  `include`. This change will not affect other plugins; just yours.

- Libraries can be added by appending to `LDFLAGS` and `CFLAGS`, for example

        LDFLAGS := $(LDFLAGS) $(shell pkg-config --ldflags eigen3)
        CFLAGS := $(CFLAGS) $(shell pkg-config --cflags eigen3)

- See the source for the exact flags.

- Inserted the path of your directory into the `plugin`-list in `config.yaml`.

## Adding a plugin (general case)

Each plugin can have a completely independent build system, as long as:
- It defines a `Makefile` with targets for `plugin.dbg.so`, `plugin.opt.so`, and `clean`. Inside
  this `Makefile`, one can defer to another build system.

- It's compiler maintains _ABI compatibility_ with the compilers used in every other plugin. Using
  the same version of Clang or GCC on the same architecture is sufficient for this.

- It's path is inserted in the root `config.yaml`, in the plugin list.

## Tutorial

With this, you can extend ILLIXR for your own purposes. You can also replace any existing
functionality this way.

1.  Create a new directory anywhere for your new plugin and set it up for ILLIXR. We recommend you also push this plugin to a git repository on Github/Gitlab if you want it as a part of upstream ILLIXR in the future.

  - Create a `Makefile` with the following contents. See [Building ILLIXR][1] for more details and alternative setups.


        include common.mk

2.  You must decide if your plugin should inherit the standardized [`threadloop`][3] or
    [`plugin`][4].

  - If your plugin just needs to run one computation repeatedly, then your plugin class should
    extend [`threadloop`][3].

  - If you need custom concurrency (more complicated than a loop), triggered concurrency (by
    events fired in other plugins), or no concurrency then your plugin class should extend
    [`plugin`][4].

3.  Write a file called `plugin.cpp` with this body, replacing every instance of `plugin_name`:

        #include "common/phonebook.hpp"
        #include "common/plugin.hpp"
        #include "common/threadloop.hpp"
        
        using namespace ILLIXR;
        
        // Inherit from `plugin` if you don't need the threadloop
        class plugin_name : public threadloop {
        public:
            plugin_name(std::string name_, phonebook* pb_)
                : threadloop{name_, pb_}
                { }
            virtual void start() override { }
            virtual ~plugin_name() override { }
        };
        
        // This line makes the plugin importable by Spindle
        PLUGIN_MAIN(plugin_name);


4. At this point, you should be able to build your plugin with ILLIXR. Move to the ILLIXR repo and update `configs/native.yaml`. If the new plugin is the same type as one of the other components you will need to remove that component from the config before running the new component. For example, if the new component is a SLAM then the old SLAM needs to be removed from the config. See [Building ILLIXR][1] for more details on the config file.

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
   

5. Finally, run ILLIXR with your new plugin with the following command: `./runner.sh configs/native.yaml`

6. This is all that is required to be a plugin which can be loaded by Spindle in the ILLIXR
   runtime. Reading and writing from Phonebook and Switchboard is optional, but nearly every plugin
   does it. See `default_plugins.md` for more details.

   First, we can query the [`phonebook`][2] to get various services including [`switchboard`][5]. Then we
   query [`switchboard`][5] for event-streams (topics). We will read `topic1`, write to `topic2`, and
   schedule computation on `topic 3`. See the API documentation for `phonebook` and `switchboard`
   for more details.


        #include "common/phonebook.hpp"
        #include "common/plugin.hpp"
        #include "common/threadloop.hpp"
    
        /* When datatypes have to be common across plugins
          (e.g. a phonebook service or switchboard topic),
           they are defined in this header,
           which is accessible to all plugins. */
        #include "common/data_format.hpp"
    
        class plugin_name : public threadloop {
        public:
            /*
                After the constructor, C++ permits a list of member-constructors.
                We use uniform initialization (curly-braces) [1] instead of parens to avoid ambiguity [2].
                We put the comma at the start of the line, so that lines can be copied around or deleted freely (except for the first).
    
                [1]: https://en.wikipedia.org/wiki/C%2B%2B11#Uniform_initialization
                [2]: https://en.wikipedia.org/wiki/Most_vexing_parse
            */
            plugin_name(std::string name_, phonebook* pb_)
                : threadloop{name_, pb_}
                  // find the switchboard in phonebook
                , sb{pb->lookup_impl<switchboard>()}
                  // create a handle to a topic in switchboard for subscribing
                , topic1{sb->subscribe_latest<topic1_type>("topic1")}
                  // create a handle to a topic in switchboard for publishing
                , topic2{sb->publish<topic2_type>("topic2")}
            {
                // Read topic 1
                topic1_type* event1 = topic1.get_latest_ro();
    
                // Write to topic 2
                topic2_type* event2 = new topic2_type;
                topic2.put(event2);
    
                // Read topic 3 synchronously
                sb->schedule<topic3_type>(get_name(), "topic3", [&](const topic3_type *event3) {
                    /*
                    This is a [lambda expression][1]
                    [1]: https://en.cppreference.com/w/cpp/language/lambda
                    */
                    std::cout << "Got a new event on topic3: " << event3 << std::endl;
                });
            }
    
            virtual void _p_one_iteration override() {
                std::cout << "Running" << std::endl;
                auto target = std::chrono::high_resolution_clock::now() +  std::chrono::milliseconds{10};
                reliable_sleep(target);
            }
    
        private:
            const std::shared_ptr<switchboard> sb;
            std::unique_ptr<reader_latest<topic1_type>> topic1;
            std::unique_ptr<writer<topic2>> topic2;
        };
    
        // This line makes the plugin importable by Spindle
        PLUGIN_MAIN(plugin_name);


[1]: building_illixr.md
[2]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1phonebook.html
[3]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1threadloop.html
[4]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1plugin.html
[5]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1switchboard.html
