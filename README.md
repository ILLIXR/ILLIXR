# ILLIXR

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

Illinois Extended Reality testbed or ILLIXR (pronounced like elixir) is
the first fully open-source Extended Reality (XR) system and testbed.
The modular, extensible, and [OpenXR][26]-compatible ILLIXR runtime
integrates state-of-the-art XR components into a complete XR system.
The testbed is part of the broader [ILLIXR consortium][37],
an industry-supported community effort to democratize XR systems
research, development, and benchmarking.

You can find the complete ILLIXR system [here][38].

ILLIXR also provides its components in standalone configurations to enable architects and
system designers to research each component in isolation.
The standalone components are packaged together in the as of the [v3.1.0 release][39] of ILLIXR.

ILLIXR's modular and extensible runtime allows adding new components and swapping different
implementations of a given component.
ILLIXR currently contains the following components:

- *Perception*
    - Eye Tracking
        1. [RITNet][3] **
    - Scene Reconstruction
        1. [ElasticFusion][2] **
        2. [KinectFusion][40] **
    - Simultaneous Localization and Mapping
        1. [OpenVINS][1] **
    - Cameras and IMUs
        1. [ZED Mini][42]
        2. [Intel RealSense][41]

- *Visual*
    - [Chromatic aberration correction][5]
    - [Computational holography for adaptive multi-focal displays][6] **
    - [Lens distortion correction][5]
    - [Asynchronous Reprojection (TimeWarp)][5]

- *Aural*
    - [Audio encoding][4] **
    - [Audio playback][4] **

(** Source is hosted in an external repository under the [ILLIXR project][7].)

We continue to add more components (new components and new implementations).

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
@ARTICLE {illixr,
author = {M. Huzaifa and R. Desai and S. Grayson and X. Jiang and Y. Jing and J. Lee and F. Lu and Y. Pang and J. Ravichandran and F. Sinclair and B. Tian and H. Yuan and J. Zhang and S. V. Adve},
journal = {IEEE Micro},
title = {ILLIXR: An Open Testbed to Enable Extended Reality Systems Research},
year = {2022},
volume = {42},
number = {04},
issn = {1937-4143},
pages = {97-106},
abstract = {We present Illinois Extended Reality testbed (ILLIXR), the first fully open-source XR system and research testbed. ILLIXR enables system innovations with end-to-end co-designed hardware, compiler, OS, and algorithms, and driven by end-user perceived Quality-of-Experience (QoE) metrics. Using ILLIXR, we provide the first comprehensive quantitative analysis of performance, power, and QoE for a complete XR system and its individual components. We describe several implications of our results that propel new directions in architecture, systems, and algorithms research for domain-specific systems in general, and XR in particular.},
keywords = {x reality;pipelines;measurement;visualization;cameras;runtime;headphones},
doi = {10.1109/MM.2022.3161018},
publisher = {IEEE Computer Society},
address = {Los Alamitos, CA, USA},
month = {jul}
}
```

## Getting Started and Documentation

For more information, see our [Getting Started page][33].

## Acknowledgements

The ILLIXR project started in [Sarita Adve’s research group][9],
co-led by PhD candidate Muhammad Huzaifa, at the University of Illinois at Urbana-Champaign.
Other major contributors include
Rishi Desai,
Doug Friedel,
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
| [concurrentqueue][P1]              | [BSD-2 clause][L1]<SUP>2</SUP>                     |
| [cxxopts][P2]                      | [MIT][L2]                                          |
| [Depthai Core][P3]                 | [MIT][L3]                                          |
| [FFmpeg][P4]                       | [GNU General Public License v3.0][L35]<SUP>3</SUP> |
| [filter][P5]                       | [MIT][L5]                                          |
| [glslang][P6]                      | [BSD-3 clause][L6]<SUP>4</SUP>                     |
| [Gtsam][P7]                        | [BSD-2 clause][L7]<SUP>2</SUP>                     |
| [imgui][P8]                        | [MIT][L8]                                          |
| [mediapipe][P36]                   | [Apache License 2.0][L36]                          |
| [Monado][P29]                      | [MIT][L29]                                         |
| [Monado integration][P9]           | [Boost v1][L9]                                     |
| [Monado Vulkan Integration][P10]   | [Boost v1][L10]                                    |
| [moodycamel::ConcurrentQueue][P30] | [BSD-2 clause][L30]<SUP>2</SUP>                    |
| [OpenCV][P11]                      | [BSD-3 clause][L11]                                |
| [Open-VINS][P35]                   | [GNU General Public License v3.0][L35]             |
| [OpenXR][P12]                      | [Boost v1][L12]                                    |
| [robin-hood-hashing][P14]          | [MIT][L14]                                         |
| [SpatialAudio][P15]                | [LGPL v2.1][L15]                                   |
| [SPIRV Headers][P16]               | [MIT][L16]                                         |
| [SPIRV Tools][P17]                 | [Apache v2][L17]                                   |
| [SqliteCPP][P18]                   | [MIT][L18]                                         |
| [stb][P19]                         | [MIT][L19]                                         |
| [tinyobjloader][P20]               | [MIT][L20]                                         |
| [Vulkan Headers][P21]              | [Apache v2][L21]                                   |
| [Vulkan Loader][P22]               | [Apache v2][L22]                                   |
| [Vulkan Utility Libraries][P23]    | [Apache v2][L23]                                   |
| [Vulkan Validation Layers][P24]    | [Apache v2][L24]                                   |
| [VulkanMemoryAllocator][P25]       | [MIT][L25]                                         |
| [yaml-cpp][P26]                    | [MIT][L26]                                         |
| zed_opencv (Sterolabs)             | [MIT][L27]                                         |

<SUP>1</SUP> Current as of April 14, 2024.

<SUP>2</SUP> Also known as the Simplified BSD License.

<SUP>3</SUP> ILLIXR uses a customized version of FFmpeg, compiled with x264 and x265 encoding support, thus it is
licensed under GPL v3.0

<SUP>4</SUP> This software is covered by multiple open source licenses, see the link for details.

The optional [Open-VINS][P35] plugin (available from a secondary repository) is licensed under [GPL v3.0][L35].

Note that ILLIXR's extensibility allows the source to be configured and compiled using only
permissively licensed software.

## Get in Touch

Whether you are a computer architect, a compiler writer, a systems person, work on XR related algorithms
or applications, or just anyone interested in XR research, development, or products,
we would love to hear from you and hope you will contribute!
You can join
the [ILLIXR consortium][37],
[Discord][47],
or [mailing list][48],
or send us an [email][49],
or just send us a pull request!


[//]: # (- References -)

[1]:    https://github.com/ILLIXR/open_vins

[2]:    https://github.com/ILLIXR/ElasticFusion

[3]:    https://github.com/ILLIXR/RITnet

[4]:    https://github.com/ILLIXR/audio_pipeline

[5]:    https://github.com/ILLIXR/visual_postprocessing

[6]:    https://github.com/ILLIXR/HOTlab

[7]:    https://github.com/ILLIXR
[8]:    https://ieeexplore.ieee.org/abstract/document/9741292

[9]:    http://rsim.cs.illinois.edu

[10]:   https://monado.dev

[11]:   https://research.fb.com/programs/openeds-challenge
[12]:   https://github.com/rpng/open_vins
[13]:   https://www.gnu.org/licenses/gpl-3.0.html
[15]:   https://github.com/luxonis/depthai-core
[16]:   https://github.com/luxonis/depthai-core/blob/main/LICENSE
[18]:   https://github.com/videolabs/libspatialaudio
[19]:   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
[21]:   https://www.gnu.org/licenses/lgpl-3.0.html
[22]:   https://gitlab.freedesktop.org/monado/monado
[23]:   https://choosealicense.com/licenses/bsl-1.0

[24]:   https://gitter.im/ILLIXR/community

[25]:   https://github.com/ILLIXR/ILLIXR/releases

[26]:   https://www.khronos.org/openxr

[33]:   https://illixr.github.io/ILLIXR/getting_started/

[34]:   https://illixr.github.io/ILLIXR/LICENSE/

[35]:   https://illixr.github.io/ILLIXR/illixr_plugins/

[36]:   https://illixr.github.io/ILLIXR/writing_your_plugin/

[37]:   http://illixr.org

[38]:   https://github.com/ILLIXR/ILLIXR

[39]:   https://github.com/ILLIXR/ILLIXR/releases/tag/v3.1.0

[41]:   https://github.com/ILLIXR/ILLIXR/tree/master/realsense

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
