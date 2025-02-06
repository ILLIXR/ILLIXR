I want to unmark this PR as a draft and open it for code review. There are some parts of this that I need some feedback and guidance on, most notably the data format and CMake integration. To that end, I will explain the design of the dataset plugin so that everyone is on the same page.


# Plugin Design

The original proposal can be read [here](https://shrub-hickory-311.notion.site/Proposal-df699e5207fa45f6acd4aa46a9000774?pvs=4). I will rewrite the most important and pertinent parts here again.

The high level overview is that, the work for the plugin modifies the YAML parsing code, and implements a system to load in and store data in memory and publish it at certain intervals. It also has some customization points to be more future-proof and robust and accomodate more varied datasets.

## Implementation Details

### YAML Side

The proposed YAML syntax is discussed at length in the proposal. I will simply copy the full example from the end of the proposal:

```yaml
delimiter: ',' # tells us what the delimiter for the data is.
# We can make the delimiter configurable and keep ',' as default.

root_path: /path/to/dataset # this tells us where the dataset is on the system
# we can have this path variable work just like the existing `data` option (as
# described here), with support for downloading files and stuff.

# All paths from this point forward are relative to `root_path`
imu:
  - timestamp_units: microseconds # somewhat fast IMU

  - path: /path/to/imu/data1
    format: true # this means that linear acceleration is first, followed by
                 # angular velocity
  - path: /path/to/imu/data2
    format: false

image:
  - timestamp_units: milliseconds
  - rgb:
      # rgb1 will be left eye, rgb2 will be right eye, etc.
      - path: /path/to/left/eye/rgb/images
      - path: /path/to/right/eye/rgb/images
      # There can be more if needed
  - depth:
      # depth1 will be left eye, depth2 will be right eye, etc.
      - path: /path/to/left/eye/depth/images
      - path: /path/to/right/eye/depth/images
      # There can be more if needed
  - grayscale:
      # grayscale1 will be Camera 1, grayscale2 will be Camera 2, etc.
      - path: /path/to/first/camera/grayscale/images
      - path: /path/to/second/camera/depth/images
      # There can be more if needed

pose:
  - timestamp_units: nanoseconds
  - path: /path/to/pose/data1
  - path: /path/to/pose/data2

ground_truth:
  - timestamp_units: nanoseconds
  - path: /path/to/groundtruth/data
```

(Note that there are some differences between the proposed YAML syntax in the proposal vs what the plugin currently supports. Where the two differ, this comment has the more up-to-date information.)

### C++ Side

The dataset plugin has 3 main internal classes, and 1 `ILLIXR`-facing class.

The internal classes are:

- `Config`: This reads data from environment variables and sets up internal flags and configuration data. For example, in the format of the IMU data, whether the linear acceleration or the angular acceleration is first, is something we have as a config flag. 
- `DatasetLoader`: Using the information retrieved by the config classes, it loads in all the data and stores it in a multimap with the timestamps as keys and the data as the values.
- `Emitter`: Since there are 4 channels of data to write, the emitter collates all the information together and then emits the relevant data at the correct time. It also has a couple of helper that allow the `Publisher` class figure out how long to make the dataset plugin sleep for, and if there is more data to emit.

The `Publisher` is the class that ties together the internal classes and establishes the link to the ILLIXR system as a whole via the writers and the managed thread model (waking up every specified internal to emit some more test data).

#### Possible Improvements

- The `Config` struct data does not need to live in memory after the dataset is loaded in via the `DatasetLoader`. Currently, it lives as a member variable in the `DatasetLoader` instance, due to the simplicity of that implementation.
- `DatasetLoader` would probably be simpler as a [POD struct](https://en.wikipedia.org/wiki/Passive_data_structure) instead of a singleton class. That simplifies a lot of the calls in the emitter and eliminates unnecessary hassle.
- The `emit` function in the `Emitter` class is not that robust currently. Whenever it wakes up, it simply emits all the dataset items up to the current time. This was done because it's a simpler implementation.
  - A far superior and smarter approach would be to construct an interval $[t_{\text{current}} - \varepsilon, t_{\text{current}}]$ (where $\varepsilon$ is a constant that is heuristically chosen), and drop anything in the list outside that interval (while informing the user), and emit everything in that interval.

All of these are enhancements that can be made after the basic system is up and running and testable.

# CMake

The CMake build system integration is a bit difficult to understand and opaque to me, an outsider.

Specifically, I need help with what files to modify to make the YAML parsing code understand the meaning of the new config files.

I know that the YAML parsing is done in CMake, but where and how exactly is information passed from the config to the ILLIXR code? For example, the path to the dataset (the `data` config value).

I have written the dataset plugin's CMake code and it builds (or at least tries to). But I need some help with the YAML parsing part.

Since it looks to me like @astro-friedel did most of the CMake work, I am pinging you for help on this matter.

## YAML to C++ Message-Passing

A key step in the pipeline for the dataset plugin is the passing of information from the YAML config file to the C++ code.

Among other things, I need to know where the data files are, some basic things about the format, etc.

My original design, pre-CMake integration, was to simply define a bunch of environment variables via the Makefile and the Python script. However, the issue with that approach is that it is somewhat brittle, hacky, and Linux-specific.

A more robust approach would be to use CMake's `configure_file` feature where it can write a bunch of `#define` macros during the configuration step. (See [here](https://cmake.org/cmake/help/latest/command/configure_file.html#transformations) for more details.) Then, my C++ code can simply read those `#define`s to get the dataset configuration info it needs. However, this `configure_file` step would also need to be done in the YAML parsing stage, which takes us back to my original question.

# Questions

The plugin, as of now, is not fully compiling yet. There are a few bugs that I need to iron out, but they require some input from the ILLIXR core devs.

## Channels

In the dataset plugin, I have taken extra care to ensure that every part (Image, IMU, Pose, Ground Truth) supports reading in data from multiple files. We have the option to also publish this information, via a `channel` parameter  (`1` means the first file, and so on). The current definitions of `imu_type`:

https://github.com/ILLIXR/ILLIXR/blob/1963c106d4850a2a5749584f265592fb8c9bd899/include/illixr/data_format.hpp#L18-L27

and `pose_type`:

https://github.com/ILLIXR/ILLIXR/blob/1963c106d4850a2a5749584f265592fb8c9bd899/include/illixr/data_format.hpp#L99-L113

Don't have a `channel` parameter. If this feature is useful, then I would need to modify those structs as required. So, is this feature something we would be interested in?

## Publishing Structs

There used to be a `cam_type` struct:

https://github.com/ILLIXR/ILLIXR/blob/59136193fb98e5a9eb78c0f6ac7fd1dd979aeaa3/common/data_format.hpp#L22-L31

which has since been removed, which I was using to publish image data. What should I use to publish it now?

The other data that I need to publish is the ground truth. We had decided (@jianxiapyh and I) that the simplest and most robust thing to do would be to perform no computations on the ground truth data, and to kind of just pass them through the dataset plugin and emit it at the correct times as a string.

However, we need a struct for that in `data_format.hpp`. So is it fine if I just add one in? The ground truth data struct would simply be a very thin wrapper around `Eigen::VectorXd`.

# ILLIXR Profiles

There are many YAML files throughout the ILLIXR repo. There is a `plugins/plugins.yaml` and there's also a bunch of profiles in the `profiles/` directory. I need some explanation of what `plugins/plugins.yaml` does, and which of the `profiles` plugins I should use to test my dataset plugin.

The end result I can envision of my work is that I can demonstrate one of the profiles using an actual dataset and moving the camera and showing the images from that dataset.

