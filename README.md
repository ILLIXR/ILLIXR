![ILLIXR logo](docs/docs/images/LogoWithHeader.png)

[![NCSA licensed](https://img.shields.io/badge/license-NCSA-blue.svg)](LICENSE)
[![ILLIXR CI](https://github.com/ILLIXR/ILLIXR/actions/workflows/ci.yaml/badge.svg)](https://github.com/ILLIXR/ILLIXR/actions/workflows/ci.yaml)
[![Discord](https://img.shields.io/discord/830812443189444698?logo=discord&logoColor=white&label=Discord)][E47]

<a href="https://www.youtube.com/watch?v=VyOShOfHt48"><img width="400" height="300" alt="Demo Video" src="https://img.youtube.com/vi/VyOShOfHt48/0.jpg"></a>

# Overview

Illinois Extended Reality testbed or ILLIXR (pronounced like elixir) is a fully open-source Extended Reality (XR)
system and testbed. The modular, extensible, and OpenXR-compatible ILLIXR runtime integrates state-of-the-art XR
components into a complete XR system.

The current ILLIXR release is [v4.0][E51]. Source code is available from our [GitHub repository][P68], which also contains
instructions for building fully contained [Docker images][E56].

ILLIXR provides its components in standalone configurations to enable architects and system designers to research each
component in isolation. The standalone components are packaged together as of the v3.1.0 release of ILLIXR.

ILLIXR's modular and extensible runtime allows adding new components and swapping different implementations of a given
component. ILLIXR currently contains the following [plugins][P66] and [services][P67]:

## Current Plugins and Services

### Perception

- Filter-based visual-inertial estimator
    - [OpenVINS][P54][^1]
- Tracking
    - Head tracking: [ORB_SLAM3][P6][^1] and [lighthouse][P55]
    - [Hand tracking][P5][^1]
    - [openni][P54]
    - [depthai][P56]
- IMU integrators
    - [gtsam_integrator][P49]
    - [passthrough_integrator][P7]
    - [rk4_integrator][P53]
- Pose Related Service
    - [fauxpose][P50]
    - [pose_prediction][P51]
    - [pose_lookup][P52]
- Cameras
    - [zed][P35] supporting [ZED Mini][E42]
    - [realsense][P36] supporting [Intel RealSense][E41]
    - [webcam][P37]

### Visual

- Asynchronous reprojection:
    - [timewarp_gl][P38], [OpenGL][E2] based
    - [timewarp_vk][P65], [Vulkan][E3] based
- Asynchronous 6-degree reprojection [openwarp][P58]
- [vkdemo][P57] - toy application, with native ILLIXR rendering interface gldemo
- [native_renderer][P59] - render management
- [gldemo][P47] - stand-in application when ILLIXR is run as a standalone application without an actual OpenXR application

### Aural

- [spatial audio encoding/playback][P1][^1]

### Data Recording

- [offload_data][P46]
- [record_imu_cam][P39]
- [record_rgb_depth][P40]
- [zed_capture][E55] (standalone executable)

### Data Reading/Injection

- [offline_cam][P41]
- [offline_imu][P42]
- [zed.data_injection][P43]
- [ground_truth_slam][P48]

### Visualization

- [debugview][P44]
- [hand_tracking.viewer][P45]

### Offloading/Remote Work

- [offload_vio][P60]
- [offload_rendering_client][P61]
- [offload_rendering_client_jetson][P62]
- [offload_rendering_server][P63]
- [tcp_network_backend][P64]

[^1]: Source is hosted in an external repository under the [ILLIXR project][E7].

Some components, such as eye tracking and reconstruction are available as standalone components and are in the process
of being integrated.

We continue to refine and add new components and implementations. Many of the current components of ILLIXR were
developed by domain experts and obtained from publicly available repositories. They were modified for one or more of the
following reasons: fixing compilation, adding features, or removing extraneous code or dependencies. Each component not
developed by us is available as a forked GitHub repository for proper attribution to its authors.

# Papers, talks, demos, consortium

A [paper][E8] with details on ILLIXR, including its components, runtime, telemetry support, and a comprehensive analysis
of performance, power, and quality on desktop and embedded systems.

A [talk presented at NVIDIA GTC'21][E42] describing ILLIXR and announcing the ILLIXR consortium:
[Video][E43].
[Slides][E44].

A [demo][E45] of an OpenXR application running with ILLIXR.

The [ILLIXR consortium][E37] is an industry-supported community effort to democratize XR systems research, development,
and benchmarking. Visit our [website][E37] for more information. For news and papers go to [illixr.org][E37], for talks
see our [YouTube][E54] channel, and join our [Discord][E47] for announcements.

## Demo Videos

All of our demo videos can be seen on our [YouTube][E54] channel. 
Here are some highlights:

| Running Locally                                                                                                                                                                                                                                                                                                                                                                      | Head Tracking (VIO) Offloaded                                                                                                                                                                       |
|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <a href="https://www.youtube.com/watch?v=XEiqujGuRyE"><img width="400" height="300" alt="Demo of ILLIXR running locally" src="https://img.youtube.com/vi/XEiqujGuRyE/0.jpg"></a>                                                                                                                                                                                                     | <a href="https://www.youtube.com/watch?v=675L1etdTMg"><img width="400" height="300" alt="Demo of ILLIXR with head tracking (VIO) offloaded" src="https://img.youtube.com/vi/675L1etdTMg/0.jpg"></a> |
| <a href="https://www.youtube.com/watch?v=VyOShOfHt48"><img width="400" height="300" alt="Demo of ILLIXR running locally" src="https://img.youtube.com/vi/VyOShOfHt48/0.jpg"></a>                                                                                                                                                                                                     | <a href="https://www.youtube.com/watch?v=2z6eufVJrJE"><img width="400" height="300" alt="Demo of ILLIXR with head tracking (VIO) offloaded" src="https://img.youtube.com/vi/2z6eufVJrJE/0.jpg"></a> |

| Rendering Offloaded                                                                                                                                                                       |
|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <a href="https://www.youtube.com/watch?v=oGBYZIPj6Zc"><img width="400" height="300" alt="Demo of ILLIXR with rendering offloaded" src="https://img.youtube.com/vi/oGBYZIPj6Zc/0.jpg"></a> |

# Citation

We request that you cite our following [paper][E8] when you use ILLIXR for a publication. We would also appreciate it if
you send us a citation once your work has been published.

```
@inproceedings{huzaifa2021illixr,
  title={ILLIXR: Enabling end-to-end extended reality research},
  author={Huzaifa, Muhammad and Desai, Rishi and Grayson, Samuel and Jiang, Xutao and Jing, Ying and Lee, Jae and Lu, Fang and Pang, Yihan and Ravichandran, Joseph and Sinclair, Finn and Tian, Boyuan and Yuan, Hengzhi and Zhang, Jeffrey and Adve, Sarita V.},
  booktitle={2021 IEEE International Symposium on Workload Characterization (IISWC)},
  pages={24--38},
  year={2021},
  organization={IEEE}
}
```

Once your work is published, please send the citation to us at [illixr@cs.illinois.edu](mailto:illixr@cs.illinois.edu).

## Contributing and Documentation

We welcome anyone to contribute to ILLIXR. If you wish to contribute, please see out contribution [guide][E53]. Full
documentation, including API specifications, can be found at our [Getting Started page][E33].

## Acknowledgements

The ILLIXR project started in [Sarita Adve’s research group][E9],
co-led by PhD candidate Muhammad Huzaifa, at the University of Illinois at Urbana-Champaign.
Other major contributors include
Rishi Desai,
Douglas Friedel,
Steven Gao,
Samuel Grayson,
Qinjun Jiang,
Xutao Jiang,
Ying Jing,
Jae Lee,
Jeffrey Liu,
Fang Lu,
Yihan Pang,
Joseph Ravichandran,
Giordano Salvador,
Rahul Singh,
Finn Sinclair,
Boyuan Tian,
Henghzhi Yuan,
and
Jeffrey Zhang.

ILLIXR came together after many consultations with researchers and practitioners in many domains:
audio,
graphics,
optics,
robotics,
signal processing,
and
extended reality systems.
We are deeply grateful for all of these discussions and specifically to the following:
Wei Cu,
Aleksandra Faust,
Liang Gao,
Matt Horsnell,
Amit Jindal,
Steve LaValle,
Steve Lovegrove,
Andrew Maimone,
Vegard &#216;ye,
Martin Persson,
Archontis Politis,
Eric Shaffer,
Paris Smaragdis,
Sachin Talathi,
and
Chris Widdowson.

Our OpenXR implementation is derived from [Monado][E10]. We are particularly thankful to Jakob Bornecrantz and Ryan
Pavlik.

The development of ILLIXR was supported by the Applications Driving Architectures (ADA) Research Center (a JUMP Center
co-sponsored by SRC and DARPA), the Center for Future Architectures Research (C-FAR, a STARnet research center), a
Semiconductor Research Corporation program sponsored by MARCO and DARPA, National Science Foundation grants 2120464 and
2217144 and by a Google Faculty Research Award. The development of ILLIXR was also aided by generous hardware and
software donations from ARM and NVIDIA. Facebook Reality Labs provided the [OpenEDS Semantic Segmentation Dataset][E11].

Wesley Darvin came up with the name for ILLIXR.

## Licensing Structure

ILLIXR is available as open-source software under the permissive [University of Illinois/NCSA Open Source License][E34].
As mentioned above, ILLIXR largely consists of components developed by domain experts and modified for the purposes of
inclusion in ILLIXR. However, ILLIXR does contain software developed solely by us. **The NCSA license is limited to only
this software**. The external libraries and software included in ILLIXR each have their own licenses and must be used
according to those licenses:

| Package                              | License[^2]                                |
|:-------------------------------------|:-------------------------------------------|
| [abseil-cpp][TPP48]                  | [Apache v2][L48]                           |
| [ARM NEON 2 x86 SSE][TPP42]          | [BSD-2 clause][L42][^3]                    |
| [concurrentqueue][TPP1]              | [BSD-2 clause][L1]                         |
| [cxxopts][TPP2]                      | [MIT][L2]                                  |
| [Depthai Core][TPP3]                 | [MIT][L3]                                  |
| [EGL-Registry][TPP47]                | None given                                 |
| [farmhash][TPP46]                    | [MIT][L46]                                 |
| [FFmpeg][TPP4]                       | [GNU General Public License v3.0][L35][^4] |
| [filter][TPP5]                       | [MIT][L5]                                  |
| [flatbuffers][TPP44]                 | [Apache v2][L44]                           |
| [gemmlowp][TPP43]                    | [Apache v2][L43]                           |
| [glslang][TPP6]                      | [BSD-3 clause][L6][^5]                     |
| [Gtsam][TPP7]                        | [BSD-2 clause][L7][^3]                     |
| [imgui][TPP8]                        | [MIT][L8]                                  |
| [mediapipe][TPP36]                   | [Apache v2][L36]                           |
| [Monado][TPP29]                      | [MIT][L29]                                 |
| [Monado integration][TPP9]           | [Boost v1][L9]                             |
| [Monado Vulkan Integration][TPP10]   | [Boost v1][L10]                            |
| [moodycamel::ConcurrentQueue][TPP30] | [BSD-2 clause][L30][^3]                    |
| [OouraFFT][TPP45]                    | [MIT-like][L45]                            |
| [Open-VINS][TPP35]                   | [GNU General Public License v3.0][L35]     |
| [OpenXR][TPP12]                      | [Boost v1][L12]                            |
| [ORB_SLAM3][TPP49]                   | [GNU General Public License v3.0][L49]     |
| [protobuf][L41]                      | [BSD-3 clause][L41]                        |
| [pthreadpool][TPP40]                 | [BSD-2 clause][L40][^3]                    |
| [robin-hood-hashing][TPP14]          | [MIT][L14]                                 |
| [ruy][TPP39]                         | [Apache v2][TPP39]                         |
| [SPIRV Headers][TPP16]               | [MIT][L16]                                 |
| [SPIRV Tools][TPP17]                 | [Apache v2][L17]                           |
| [SqliteCPP][TPP18]                   | [MIT][L18]                                 |
| [stb][TPP19]                         | [MIT][L19]                                 |
| [tensorflow-lite][TPP38]             | [Apache v2][L38]                           |
| [tinyobjloader][TPP20]               | [MIT][L20]                                 |
| [Vulkan Headers][TPP21]              | [Apache v2][L21]                           |
| [Vulkan Loader][TPP22]               | [Apache v2][L22]                           |
| [Vulkan Utility Libraries][TPP23]    | [Apache v2][L23]                           |
| [Vulkan Validation Layers][TPP24]    | [Apache v2][L24]                           |
| [VulkanMemoryAllocator][TPP25]       | [MIT][L25]                                 |
| [XNNPACK][TPP37]                     | [BSD-3 clause][L37]                        |
| [yaml-cpp][TPP26]                    | [MIT][L26]                                 |
| zed_opencv (Sterolabs)               | [MIT][L27]                                 |

[^2]: Current as of March 5, 2025.

[^3]: Also known as the Simplified BSD License.

[^4]: ILLIXR uses a customized version of FFmpeg, compiled with x264 and x265 encoding support, thus it is licensed under GPL v3.0

[^5]: This software is covered by multiple open source licenses, see the link for details.

Any LGPL or GPL licensed code are contained in optional components. ILLIXR's extensibility allows the source to be
configured and compiled using only permissively licensed software, if desired. See out [Getting Started][E33] page for
instructions.

## Get in Touch

Whether you are a computer architect, a compiler writer, a systems person, work on XR related algorithms or
applications, or just anyone interested in XR research, development, or products, we would love to hear from you and
hope you will contribute! You can join our [Discord][E47], [mailing list][E48], [email][E49] us, or just send a pull
request!


[//]: # (- external -)

[E2]:    https://www.opengl.org/

[E3]:    https://www.vulkan.org/

[E4]:    https://github.com/ILLIXR/audio_pipeline

[E7]:    https://github.com/ILLIXR

[E8]:    https://ieeexplore.ieee.org/abstract/document/9741292

[E9]:    http://rsim.cs.illinois.edu

[E10]:   https://monado.dev

[E11]:   https://research.fb.com/programs/openeds-challenge

[E26]:   https://www.khronos.org/openxr

[E33]:   https://illixr.github.io/ILLIXR/getting_started/

[E34]:   https://illixr.github.io/ILLIXR/LICENSE/

[E35]:   https://illixr.github.io/ILLIXR/illixr_plugins/

[E36]:   https://illixr.github.io/ILLIXR/writing_your_plugin/

[E37]:   http://illixr.org

[E38]:   https://github.com/ILLIXR/ILLIXR

[E39]:   https://github.com/ILLIXR/ILLIXR/releases/tag/v3.1.0

[E41]:   https://www.intelrealsense.com/

[E42]:   https://www.stereolabs.com/zed-mini/

[E43]:   https://youtu.be/ZY98lWksnpM

[E44]:   https://ws.engr.illinois.edu/sitemanager/getfile.asp?id=2971

[E45]:   https://youtu.be/GVcCW8WgEDY

[E47]:   https://discord.gg/upkvy7x3W4

[E48]:   mailto:lists@lists.cs.illinois.edu?subject=sub%20illixr-community

[E49]:   mailto:illixr@cs.illinois.edu

[E50]:   https://illixr.org/open_meetings

[E51]:   https://github.com/ILLIXR/ILLIXR/releases/latest

[E52]:   https://illixr.github.io/ILLIXR/docker/

[E53]:   https://illixr.github.io/ILLIXR/contributing/contributing/index.html

[E54]:   https://www.youtube.com/@sadve-group

[E55]:   https://illixr.github.io/ILLIXR/plugin_README/zed_capture/

[E56]:   https://github.com/ILLIXR/ILLIXR/tree/master/docker

[//]: # (- Plugins -)

[P1]:    https://illixr.github.io/ILLIXR/illixr_plugins/index.html#audio_pipeline

[P5]:    https://illixr.github.io/ILLIXR/illixr_plugins/index.html#hand_tracking

[P6]:    https://illixr.github.io/ILLIXR/illixr_plugins/index.html#orb_slam3

[P7]:    https://illixr.github.io/ILLIXR/illixr_plugins/index.html#passthrough_integrator

[P35]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#zed

[P36]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#realsense

[P37]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#webcam

[P38]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#timewarp_gl

[P39]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#record_imu_cam

[P40]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#record_rgb_depth

[P41]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offline_cam

[P42]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offline_imu

[P43]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#zeddata_injection

[P44]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#debugview

[P45]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#hand_trackingviewer

[P46]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offload_data

[P47]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#gldemo

[P48]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#ground_truth_slam

[P49]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#gtsam_integrator

[P50]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#fauxpose

[P51]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#pose_prediction

[P52]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#pose_lookup

[P53]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#rk4_integrator

[P54]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#openni

[P55]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#lighthouse

[P56]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#depthai

[P57]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#vkdemo

[P58]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#openwarp_vk

[P59]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#native_renderer

[P60]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offload_vio

[P61]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offload_rendering_client

[P62]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offload_rendering_client_jetson

[P63]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#offload_rendering_server

[P64]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#tcp_network_backend

[P65]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#timewarp_vk

[P66]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html

[P67]:   https://illixr.github.io/ILLIXR/illixr_services/index.html

[P68]:   https://github.com/ILLIXR/ILLIXR


[//]: # (- Third Party Packages -)

[TPP1]:   https://github.com/cameron314/concurrentqueue

[TPP2]:   https://github.com/jarro2783/cxxopts

[TPP3]:   https://github.com/luxonis/depthai-core

[TPP4]:   https://github.com/ILLIXR/FFmpeg/

[TPP5]:   https://github.com/casiez/OneEuroFilter

[TPP6]:   https://github.com/KhronosGroup/glslang

[TPP7]:   https://github.com/borglab/gtsam

[TPP8]:   https://github.com/ocornut/imgui

[TPP9]:   https://github.com/ILLIXR/monado_integration

[TPP10]:   https://github.com/ILLIXR/monado_vulkan_integration

[TPP11]:   https://github.com/ILLIXR/opencv

[TPP12]:   https://github.com/ILLIXR/Monado_OpenXR_Simple_Example

[TPP13]:   https://github.com/PortAudio/portaudio

[TPP14]:   https://github.com/martinus/robin-hood-hashing

[TPP16]:   https://github.com/KhronosGroup/SPIRV-Headers

[TPP17]:   https://github.com/KhronosGroup/SPIRV-Tools

[TPP18]:   https://github.com/iwongu/sqlite3pp

[TPP19]:   https://github.com/nothings/stb

[TPP20]:   https://github.com/tinyobjloader/tinyobjloader

[TPP21]:   https://github.com/KhronosGroup/Vulkan-Headers

[TPP22]:   https://github.com/KhronosGroup/Vulkan-Loader

[TPP23]:   https://github.com/KhronosGroup/Vulkan-Utility-Libraries

[TPP24]:   https://github.com/KhronosGroup/Vulkan-ValidationLayers

[TPP25]:   https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/blob/master/include/vk_mem_alloc.h

[TPP26]:   https://github.com/jbeder/yaml-cpp

[TPP28]:   https://github.com/videolabs/libspatialaudio

[TPP29]:   https://gitlab.freedesktop.org/monado/monado

[TPP30]:   https://github.com/cameron314/concurrentqueue

[TPP31]:   https://github.com/mp3guy/ElasticFusion

[TPP32]:   https://github.com/ILLIXR/KinectFusionApp/tree/illixr-integration

[TPP33]:   https://github.com/MartinPersson/HOTlab

[TPP34]:   https://github.com/AayushKrChaudhary/RITnet

[TPP35]:   https://github.com/rpng/open_vins

[TPP36]:   https://github.com/google-ai-edge/mediapipe

[TPP37]:   https://github.com/ILLIXR/XNNPACK

[TPP38]:   https://github.com/ILLIXR/tensorflow-lite

[TPP39]:   https://github.com/ILLIXR/ruy

[TPP40]:   https://github.com/Maratyszcza/pthreadpool

[TPP41]:   https://github.com/protocolbuffers/protobuf

[TPP42]:   https://github.com/intel/ARM_NEON_2_x86_SSE

[TPP43]:   https://github.com/google/gemmlowp

[TPP44]:   https://github.com/google/flatbuffers

[TPP45]:   https://github.com/petewarden/OouraFFT

[TPP46]:   https://github.com/google/farmhash

[TPP47]:   https://github.com/KhronosGroup/EGL-Registry

[TPP48]:   https://github.com/abseil/abseil-cpp

[TPP49]:   https://github.com/ILLIXR/ORB_SLAM3

[//]: # (- Licenses -)

[L1]:   https://github.com/cameron314/concurrentqueue/blob/master/LICENSE.md

[L2]:   https://github.com/jarro2783/cxxopts/blob/master/LICENSE

[L3]:   https://github.com/luxonis/depthai-core/blob/main/LICENSE

[L4]:   https://github.com/ILLIXR/FFmpeg/blob/master/LICENSE.md

[L5]:   https://github.com/ILLIXR/ILLIXR/blob/master/plugins/gtsam_integrator/third_party/filter.h

[L6]:   https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt

[L7]:   https://github.com/borglab/gtsam/blob/develop/LICENSE

[L8]:   https://github.com/ocornut/imgui/blob/master/LICENSE.txt

[L9]:   https://github.com/ILLIXR/monado_integration/blob/master/LICENSE

[L10]:   https://github.com/ILLIXR/monado_vulkan_integration/blob/main/LICENSE

[L11]:   https://github.com/ILLIXR/opencv/blob/master/LICENSE

[L12]:   https://github.com/ILLIXR/Monado_OpenXR_Simple_Example/blob/master/LICENSE

[L13]:   https://github.com/PortAudio/portaudio/blob/master/LICENSE.txt

[L14]:   https://github.com/martinus/robin-hood-hashing/blob/master/LICENSE

[L16]:   https://github.com/KhronosGroup/SPIRV-Headers/blob/main/LICENSE

[L17]:   https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE

[L18]:   https://github.com/iwongu/sqlite3pp/blob/master/LICENSE

[L19]:   https://github.com/nothings/stb/blob/master/LICENSE

[L20]:   https://github.com/tinyobjloader/tinyobjloader/blob/release/LICENSE

[L21]:   https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md

[L22]:   https://github.com/KhronosGroup/Vulkan-Loader/blob/main/LICENSE.txt

[L23]:   https://github.com/KhronosGroup/Vulkan-Utility-Libraries/blob/main/LICENSE.md

[L24]:   https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/LICENSE.txt

[L25]:   https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/blob/master/LICENSE.txt

[L26]:   https://github.com/jbeder/yaml-cpp/blob/master/LICENSE

[L27]:   https://github.com/ILLIXR/ILLIXR/blob/master/plugins/zed/include/LICENSE

[L28]:   https://github.com/videolabs/libspatialaudio/blob/master/LICENSE

[L29]:   https://gitlab.freedesktop.org/monado/monado/-/blob/main/LICENSES/LicenseRef-Khronos-Free-Use-License-for-Software-and-Documentation.txt

[L30]:   https://github.com/cameron314/concurrentqueue/blob/master/LICENSE.md

[L31]:   https://github.com/mp3guy/ElasticFusion/blob/master/LICENSE.txt

[L32]:   https://github.com/chrdiller/KinectFusionApp/blob/master/LICENSE.txt

[L33]:   https://github.com/MartinPersson/HOTlab/blob/master/docs%20and%20license/COPYING.LESSER.txt

[L34]:   https://github.com/AayushKrChaudhary/RITnet/blob/master/License.md

[L35]:   https://www.gnu.org/licenses/gpl-3.0.html

[L36]:   https://github.com/google-ai-edge/mediapipe/blob/master/LICENSE

[L37]:   https://github.com/ILLIXR/XNNPACK/blob/master/LICENSE

[L38]:   https://github.com/ILLIXR/tensorflow-lite/blob/main/LICENSE

[L39]:   https://github.com/ILLIXR/ruy/blob/master/LICENSE

[L40]:   https://github.com/Maratyszcza/pthreadpool/blob/master/LICENSE

[L41]:   https://github.com/protocolbuffers/protobuf/blob/main/LICENSE

[L42]:   https://github.com/intel/ARM_NEON_2_x86_SSE/blob/master/LICENSE

[L43]:   https://github.com/google/gemmlowp/blob/master/LICENSE

[L44]:   https://github.com/google/flatbuffers/blob/master/LICENSE

[L45]:   https://github.com/petewarden/OouraFFT?tab=readme-ov-file

[L46]:   https://github.com/google/farmhash?tab=MIT-1-ov-file#readme

[L48]:   https://github.com/abseil/abseil-cpp/blob/master/LICENSE

[L49]:  https://github.com/ILLIXR/ORB_SLAM3/blob/master/LICENSE
