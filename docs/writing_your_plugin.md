# Writing your plugin

With this, you can extend ILLIXR for your own purposes. You can also replace any existing
functionality this way.

1.  We suggest making a subdirectory in the ILLIXR repo, but one could make it anywhere.

  - Add this directory to `plugins` in ILLIXR's root `Makefile`. The order in this list determines
   the order of initialization in the program. [`phonebook`][2], for example, is order-sensitive.

  - In your plugin directory, we suggest symlinking common (`ln -s ../common common`).

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
            plugin_name(phonebook* pb) { }
            virtual void start() override { }
            virtual ~plugin_name() override { }
        };

        // This line makes the plugin importable by Spindle
        PLUGIN_MAIN(plugin_name);


4.  At this point, you should be able to go to the ILLIXR root and `make dbg`. If you edit a source
    file and then `make dbg`, it should trigger a rebuild of your plugin.

5.  This is all that is required to be a plugin which can be loaded by Spindle in the ILLIXR
    runtime. Reading and writing from Phonebook and Switchboard optional, but nearly every plugin
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
        #include "common/data_formath.hpp"

        class plugin_name : public threadloop {
        public:
            /*
                After the constructor, C++ permits a list of member-constructors.
                We use uniform initialization (curly-braces) [1] instead of parens to avoid ambiguity [2].
                We put the comma at the start of the line, so that lines can be copied around or deleted freely (except for the first).

                [1]: https://en.wikipedia.org/wiki/C%2B%2B11#Uniform_initialization
                [2]: https://en.wikipedia.org/wiki/Most_vexing_parse
            */
            plugin_name(const phonebook* pb)
                  // find the switchboard in phonebook
                : sb{pb->lookup_impl<switchboard>()}
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
                sb->schedule<topic3_type>("topic3", [&](const topic3_type *event3) {
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
            const std::shared_ptr<switchboard> pb;
            std::unique_ptr<reader_latest<topic1_type>> topic1;
            std::unique_ptr<writer<topic2>> topic2;
        };

        // This line makes the plugin importable by Spindle
        PLUGIN_MAIN(plugin_name);


[1]: building_ILLIXR.md
[2]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1phonebook.html
[3]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1threadloop.html
[4]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1plugin.html
[5]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1switchboard.html
