![ILLIXR logo](docs/docs/images/LogoWithHeader.png)

[![NCSA licensed](https://img.shields.io/badge/license-NCSA-blue.svg)](LICENSE)
[![ILLIXR CI](https://github.com/ILLIXR/ILLIXR/actions/workflows/ci.yaml/badge.svg)](https://github.com/ILLIXR/ILLIXR/actions/workflows/ci.yaml)
[![Discord](https://img.shields.io/discord/830812443189444698?logo=discord&logoColor=white&label=Discord)][47]


<a href="https://youtu.be/GVcCW8WgEDY">
    <img
        alt="ILLIXR Simple Demo"
        src="https://img.youtube.com/vi/GVcCW8WgEDY/0.jpg"
        style="width: 320px"
        class="center"
    >
</a>

Illinois Extended Reality testbed or ILLIXR (pronounced like elixir) is the first fully open-source Extended Reality (
XR) system and testbed. The modular, extensible, and [OpenXR][26]-compatible ILLIXR runtime integrates state-of-the-art
XR components into a complete XR system. The testbed is part of the broader [ILLIXR consortium][37], an
industry-supported community effort to democratize XR systems research, development, and benchmarking.

The current ILLIXR release is [v3.3][51]. Source code is available from our GitHub [repository][38], which also contains
instructions for building fully contained Docker images. Docker images can also be downloaded from a hub (see [here][52]
for instructions.)

ILLIXR provides its components in standalone configurations to enable architects and
system designers to research each component in isolation.
The standalone components are packaged together in the as of the [v3.1.0 release][39] of ILLIXR.

ILLIXR's modular and extensible runtime allows adding new components and swapping different
implementations of a given component.
ILLIXR currently contains the following components:

- *Perception*
    - Simultaneous Localization and Mapping
        1. [OpenVINS][1]<SUP>1</SUP>
    - Cameras and IMUs
        1. [zed][PL35] supporting [ZED Mini][42]
        2. [realsense][PL36] supporting [Intel RealSense][41]
        3. [webcam][PL37]

- *Visual*
    - [Chromatic aberration correction]
    - [Lens distortion correction]
    - Asynchronous Reprojection [TimeWarp][PL38], both [OpenGL][2] and [Vulkan][3] based

- *Detection and Tracking*
    - Head tracking [ORB_SLAM3][6]<SUP>1</SUP>
    - [Hand tracking][5]<SUP>1</SUP>
    - [openni][PL54]
    - [lighthouse][PL55]
    - [depthai][PL56]

- *Aural*
    - [Audio encoding][4]<SUP>1</SUP>
    - [Audio playback][4]<SUP>1</SUP>

- *Data Recording*
    - [offload_data][PL46]
    - [record_imu_cam][PL39]
    - [record_rgb_depth][PL40]
    - zed_capture (standalone executable)

- *Data Reading/Injection*
    - [offline_cam][PL41]
    - [offline_imu][PL42]
    - [zed.data_injection][PL43]
    - [vkdemo][PL57]

- *Visualization*
    - [debugview][PL44]
    - [hand_tracking.viewer][PL45]

- *Renderers/Reprojection*
    - [gldemo][PL47]
    - [openwarp][PL58]
    - [native_renderer][PL59]

- *Data Testing*
    - [ground_truth_slam][PL48]

- *Pose Providers*
    - [gtsam_integrator][PL49]
    - [fauxpose][PL50]
    - [pose_prediction][PL51]
    - [pose_lookup][PL52]
    - [rk4_integrator][PL53]

- *Offloading/Remote Work*
    - [offload_vio][PL60]
    - [offload_rendering_client][PL61]
    - [offload_rendering_client_jetson][PL62]
    - [offload_rendering_server][PL63]
    - [tcp_network_backend][PL64]

<SUP>1</SUP> Source is hosted in an external repository under the [ILLIXR project][7].
<BR><SUP>2</SUP> Source is in the main ILLIXR [repository][38] under plugins.

We continue to refine and add new components and implementations.
Many of the current components of ILLIXR were developed by domain experts and obtained from
publicly available repositories.
They were modified for one or more of the following reasons: fixing compilation, adding features,
or removing extraneous code or dependencies.
Each component not developed by us is available as a forked GitHub repository for
proper attribution to its authors.

# Papers, talks, demos, consortium

A [paper][8] with details on ILLIXR, including its components, runtime, telemetry support,
and a comprehensive analysis of performance, power, and quality on desktop and embedded systems.

A [talk presented at NVIDIA GTC'21][42] describing ILLIXR and announcing the ILLIXR consortium:
[Video][43].
[Slides][44].

A [demo][45] of an OpenXR application running with ILLIXR.

For more up-to-date list of related papers, demos, and talks, please visit [illixr.org][37].

The [ILLIXR consortium][37] is an industry-supported community effort to democratize
XR systems research, development, and benchmarking.
Visit our [website][37] for more information.

The ILLIXR consortium is also holding a biweekly consortium meeting. For past meetings, for more information, past
meeting recordings, and request for presenting, please visit [here][50]. Please join our [Discord][47] for announcement.

# Citation

We request that you cite our following [paper][8] when you use ILLIXR for a publication.
We would also appreciate it if you send us a citation once your work has been published.

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

## Contributing and Documentation

We welcome anyone to contribute to ILLIXR. If you wish to contribute, please see out contribution [guide][53]. Full
documentation, including API specifications, can be found at our [Getting Started page][33].

## Acknowledgements

The ILLIXR project started in [Sarita Adve’s research group][9],
co-led by PhD candidate Muhammad Huzaifa, at the University of Illinois at Urbana-Champaign.
Other major contributors include
Rishi Desai,
Douglas Friedel,
Samuel Grayson,
Xutao Jiang,
Ying Jing,
Jae Lee,
Fang Lu,
Yihan Pang,
Joseph Ravichandran,
Giordano Salvador,
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

Our OpenXR implementation is derived from [Monado][10].
We are particularly thankful to Jakob Bornecrantz and Ryan Pavlik.

The development of ILLIXR was supported by
the Applications Driving Architectures (ADA) Research Center
(a JUMP Center co-sponsored by SRC and DARPA),
the Center for Future Architectures Research (C-FAR, a STARnet research center),
a Semiconductor Research Corporation program sponsored by MARCO and DARPA,
and
by a Google Faculty Research Award.
The development of ILLIXR was also aided by generous hardware and software donations
from ARM and NVIDIA.
Facebook Reality Labs provided the [OpenEDS Semantic Segmentation Dataset][11].

Wesley Darvin came up with the name for ILLIXR.

## Licensing Structure

ILLIXR is available as open-source software under the permissive
[University of Illinois/NCSA Open Source License][34].
As mentioned above, ILLIXR largely consists of components developed by domain experts and
modified for the purposes of inclusion in ILLIXR.
However, ILLIXR does contain software developed solely by us.
**The NCSA license is limited to only this software**.
The external libraries and software included in ILLIXR each have their own licenses and
must be used according to those licenses:

| Package                            | License<SUP>1</SUP>                                |
|:-----------------------------------|:---------------------------------------------------|
| [abseil-cpp][P48]                  | [Apache v2][L48]                                   |
| [ARM NEON 2 x86 SSE][P42]          | [BSD-2 clause][L42]<SUP>2</SUP>                    |
| [concurrentqueue][P1]              | [BSD-2 clause][L1]                                 |
| [cxxopts][P2]                      | [MIT][L2]                                          |
| [Depthai Core][P3]                 | [MIT][L3]                                          |
| [EGL-Registry][P47]                | None given                                         |
| [farmhash][P46]                    | [MIT][L46]                                         |
| [FFmpeg][P4]                       | [GNU General Public License v3.0][L35]<SUP>3</SUP> |
| [filter][P5]                       | [MIT][L5]                                          |
| [flatbuffers][P44]                 | [Apache v2][L44]                                   |
| [gemmlowp][P43]                    | [Apache v2][L43]                                   |
| [glslang][P6]                      | [BSD-3 clause][L6]<SUP>4</SUP>                     |
| [Gtsam][P7]                        | [BSD-2 clause][L7]<SUP>2</SUP>                     |
| [imgui][P8]                        | [MIT][L8]                                          |
| [libspatialaudio][P28]             | [LGPL v2.1][L28]                                   |
| [mediapipe][P36]                   | [Apache v2][L36]                                   |
| [Monado][P29]                      | [MIT][L29]                                         |
| [Monado integration][P9]           | [Boost v1][L9]                                     |
| [Monado Vulkan Integration][P10]   | [Boost v1][L10]                                    |
| [moodycamel::ConcurrentQueue][P30] | [BSD-2 clause][L30]<SUP>2</SUP>                    |
| [OouraFFT][P45]                    | [MIT-like][L45]                                    |
| [Open-VINS][P35]                   | [GNU General Public License v3.0][L35]             |
| [OpenXR][P12]                      | [Boost v1][L12]                                    |
| [PortAudio][P13]                   | [MIT][L13]                                         |
| [ORB_SLAM3][P49]                   | [GNU General Public License v3.0][L49]             |
| [protobuf][L41]                    | [BSD-3 clause][L41]                                |
| [pthreadpool][P40]                 | [BSD-2 clause][L40]<SUP>2</SUP>                    |
| [robin-hood-hashing][P14]          | [MIT][L14]                                         |
| [ruy][P39]                         | [Apache v2][P39]                                   |
| [SPIRV Headers][P16]               | [MIT][L16]                                         |
| [SPIRV Tools][P17]                 | [Apache v2][L17]                                   |
| [SqliteCPP][P18]                   | [MIT][L18]                                         |
| [stb][P19]                         | [MIT][L19]                                         |
| [tensorflow-lite][P38]             | [Apache v2][L38]                                   |
| [tinyobjloader][P20]               | [MIT][L20]                                         |
| [Vulkan Headers][P21]              | [Apache v2][L21]                                   |
| [Vulkan Loader][P22]               | [Apache v2][L22]                                   |
| [Vulkan Utility Libraries][P23]    | [Apache v2][L23]                                   |
| [Vulkan Validation Layers][P24]    | [Apache v2][L24]                                   |
| [VulkanMemoryAllocator][P25]       | [MIT][L25]                                         |
| [XNNPACK][P37]                     | [BSD-3 clause][L37]                                |
| [yaml-cpp][P26]                    | [MIT][L26]                                         |
| zed_opencv (Sterolabs)             | [MIT][L27]                                         |

<SUP>1</SUP> Current as of March 5, 2025.

<SUP>2</SUP> Also known as the Simplified BSD License.

<SUP>3</SUP> ILLIXR uses a customized version of FFmpeg, compiled with x264 and x265 encoding support, thus it is
licensed under GPL v3.0

<SUP>4</SUP> This software is covered by multiple open source licenses, see the link for details.

Any LGPL or GPL licensed code are contained in optional components. ILLIXR's extensibility allows the source to be
configured and compiled using only
permissively licensed software, if desired. See out [Getting Started][33] page for instructions.

## Get in Touch

Whether you are a computer architect, a compiler writer, a systems person, work on XR related algorithms
or applications, or just anyone interested in XR research, development, or products, we would love to hear from you and
hope you will contribute!
You can join the [ILLIXR consortium][37], [Discord][47], or [mailing list][48], or send us an [email][49], or just send
us a pull request!


[//]: # (- References -)

[1]:    https://github.com/ILLIXR/open_vins

[2]:    https://www.opengl.org/

[3]:    https://www.vulkan.org/

[4]:    https://github.com/ILLIXR/audio_pipeline

[5]:    https://github.com/ILLIXR/hand_tracking

[6]:    https://github.com/ILLIXR/ORB_SLAM3

[7]:    https://github.com/ILLIXR

[8]:    https://ieeexplore.ieee.org/abstract/document/9741292

[9]:    http://rsim.cs.illinois.edu

[10]:   https://monado.dev

[11]:   https://research.fb.com/programs/openeds-challenge

[26]:   https://www.khronos.org/openxr

[33]:   https://illixr.github.io/ILLIXR/getting_started/

[34]:   https://illixr.github.io/ILLIXR/LICENSE/

[35]:   https://illixr.github.io/ILLIXR/illixr_plugins/

[36]:   https://illixr.github.io/ILLIXR/writing_your_plugin/

[37]:   http://illixr.org

[38]:   https://github.com/ILLIXR/ILLIXR

[39]:   https://github.com/ILLIXR/ILLIXR/releases/tag/v3.1.0

[41]:   https://www.intelrealsense.com/

[42]:   https://www.stereolabs.com/zed-mini/

[43]:   https://youtu.be/ZY98lWksnpM

[44]:   https://ws.engr.illinois.edu/sitemanager/getfile.asp?id=2971

[45]:   https://youtu.be/GVcCW8WgEDY

[47]:   https://discord.gg/upkvy7x3W4

[48]:   mailto:lists@lists.cs.illinois.edu?subject=sub%20illixr-community

[49]:   mailto:illixr@cs.illinois.edu

[50]:   https://illixr.org/open_meetings

[51]:   https://ffmpeg.org/

[52]:   https://github.com/ILLIXR/opencv

[53]:   https://github.com/opencv/opencv/blob/4.x/LICENSE

[54]:   https://github.com/ILLIXR/Monado_OpenXR_Simple_Example

[56]:   https://github.com/PortAudio/portaudio

[57]:   https://github.com/PortAudio/portaudio/blob/master/LICENSE.txt

[58]:   https://github.com/jbeder/yaml-cpp

[59]:   https://github.com/jbeder/yaml-cpp/blob/master/LICENSE

[60]:   https://github.com/KhronosGroup

[61]:   https://choosealicense.com/licenses/apache-2.0/

[62]:   https://choosealicense.com/licenses/mit/

[71]:   https://github.com/google-ai-edge/mediapipe

[72]:   https://github.com/google-ai-edge/mediapipe/blob/master/LICENSE

[51]:   https://github.com/ILLIXR/ILLIXR/releases/tag/v3.3.0

[52]:   https://illixr.github.io/ILLIXR/docker/

[53]:   https://illixr.github.io/ILLIXR/contributing/contributing/

[//]: # (- Plugins -)

[PL35]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-zed

[PL36]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-realsense

[PL37]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-webcam

[PL38]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-timewarp

[PL39]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-record-imu-cam

[PL40]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-record-rgb-depth

[PL41]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offline-cam

[PL42]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offline-imu

[PL43]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-zed-data-injection

[PL44]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-debugview

[PL45]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-hand-tracking-viewer

[PL46]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offload-data

[PL47]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-gldemo

[PL48]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-ground-truth-slam

[PL49]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-gtsam-integrator

[PL50]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#service-fauxpose

[PL51]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#service-pose-prediction

[PL52]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#service-pose-lookup

[PL53]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-rk4-integrator

[PL54]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-openni

[PL55]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-lighthouse

[PL56]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-depthai

[PL57]:   https://illixr.github.io/ILLIXR/illixr_services/index.html#service-vkdemo

[PL58]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-openwarp-vk

[PL59]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-native-renderer

[PL60]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offload-vio

[PL61]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offload-rendering-client

[PL62]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offload-rendering-client-jetson

[PL63]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-offload-rendering-server

[PL64]:   https://illixr.github.io/ILLIXR/illixr_plugins/index.html#plugins-tcp-network-backend


[//]: # (- Third Party Packages -)

[P1]:   https://github.com/cameron314/concurrentqueue

[P2]:   https://github.com/jarro2783/cxxopts

[P3]:   https://github.com/luxonis/depthai-core

[P4]:   https://github.com/ILLIXR/FFmpeg/

[P5]:   https://github.com/casiez/OneEuroFilter

[P6]:   https://github.com/KhronosGroup/glslang

[P7]:   https://github.com/borglab/gtsam

[P8]:   https://github.com/ocornut/imgui

[P9]:   https://github.com/ILLIXR/monado_integration

[P10]:   https://github.com/ILLIXR/monado_vulkan_integration

[P11]:   https://github.com/ILLIXR/opencv

[P12]:   https://github.com/ILLIXR/Monado_OpenXR_Simple_Example

[P13]:   https://github.com/PortAudio/portaudio

[P14]:   https://github.com/martinus/robin-hood-hashing

[P15]:   https://github.com/ILLIXR/libspatialaudio

[P16]:   https://github.com/KhronosGroup/SPIRV-Headers

[P17]:   https://github.com/KhronosGroup/SPIRV-Tools

[P18]:   https://github.com/iwongu/sqlite3pp

[P19]:   https://github.com/nothings/stb

[P20]:   https://github.com/tinyobjloader/tinyobjloader

[P21]:   https://github.com/KhronosGroup/Vulkan-Headers

[P22]:   https://github.com/KhronosGroup/Vulkan-Loader

[P23]:   https://github.com/KhronosGroup/Vulkan-Utility-Libraries

[P24]:   https://github.com/KhronosGroup/Vulkan-ValidationLayers

[P25]:   https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/blob/master/include/vk_mem_alloc.h

[P26]:   https://github.com/jbeder/yaml-cpp

[P28]:   https://github.com/videolabs/libspatialaudio

[P29]:   https://gitlab.freedesktop.org/monado/monado

[P30]:   https://github.com/cameron314/concurrentqueue

[P31]:   https://github.com/mp3guy/ElasticFusion

[P32]:   https://github.com/ILLIXR/KinectFusionApp/tree/illixr-integration

[P33]:   https://github.com/MartinPersson/HOTlab

[P34]:   https://github.com/AayushKrChaudhary/RITnet

[P35]:   https://github.com/rpng/open_vins

[P36]:   https://github.com/google-ai-edge/mediapipe

[P37]:   https://github.com/ILLIXR/XNNPACK

[P38]:   https://github.com/ILLIXR/tensorflow-lite

[P39]:   https://github.com/ILLIXR/ruy

[P40]:   https://github.com/Maratyszcza/pthreadpool

[P41]:   https://github.com/protocolbuffers/protobuf

[P42]:   https://github.com/intel/ARM_NEON_2_x86_SSE

[P43]:   https://github.com/google/gemmlowp

[P44]:   https://github.com/google/flatbuffers

[P45]:   https://github.com/petewarden/OouraFFT

[P46]:   https://github.com/google/farmhash

[P47]:   https://github.com/KhronosGroup/EGL-Registry

[P48]:   https://github.com/abseil/abseil-cpp

[P49]:   https://github.com/ILLIXR/ORB_SLAM3

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

[L15]:   https://github.com/ILLIXR/libspatialaudio/blob/master/LICENSE

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
