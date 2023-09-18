# ILLIXR

[![NCSA licensed](https://img.shields.io/badge/license-NCSA-blue.svg)](LICENSE)
[![CI](https://github.com/ILLIXR/ILLIXR/workflows/illixr-tests-master/badge.svg)](https://github.com/ILLIXR/ILLIXR/actions)
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

-   *Perception*
    -   Eye Tracking
        1.  [RITNet][3] **
    -   Scene Reconstruction
        1.  [ElasticFusion][2] **
        2.  [KinectFusion][40] **
    -   Simultaneous Localization and Mapping
        1.  [OpenVINS][1] **
    -   Cameras and IMUs
        1.  [ZED Mini][42]
        2.  [Intel RealSense][41]

-   *Visual*
    -   [Chromatic aberration correction][5]
    -   [Computational holography for adaptive multi-focal displays][6] **
    -   [Lens distortion correction][5]
    -   [Asynchronous Reprojection (TimeWarp)][5]

-   *Aural*
    -   [Audio encoding][4] **
    -   [Audio playback][4] **

(** Source is hosted in an external repository under the [ILLIXR project][7].)

We continue to add more components (new components and new implementations). 

Many of the current components of ILLIXR were developed by domain experts and obtained from
    publicly available repositories.
They were modified for one or more of the following reasons: fixing compilation, adding features,
    or removing extraneous code or dependencies.
Each component not developed by us is available as a forked github repository for
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
Visit our [web site][37] for more information.

The ILLIXR consortium is also holding a biweekly consortium meeting. For past meetings, for more information, past meeting recordings, and request for presenting, please visit [here][50]. Please join our [Discord][47] for announcement. 

# Citation

We request that you cite our following [paper][8] when you use ILLIXR for a publication.
We would also appreciate it if you send us a citation once your work has been published.

```
@inproceedings{HuzaifaDesai2021,
  author={Huzaifa, Muhammad and Desai, Rishi and Grayson, Samuel and Jiang, Xutao and Jing, Ying and Lee, Jae and Lu, Fang and Pang, Yihan and Ravichandran, Joseph and Sinclair, Finn and Tian, Boyuan and Yuan, Hengzhi and Zhang, Jeffrey and Adve, Sarita V.},
  booktitle={2021 IEEE International Symposium on Workload Characterization (IISWC)}, 
  title={ILLIXR: Enabling End-to-End Extended Reality Research}, 
  year={2021},
  volume={},
  number={},
  pages={24-38},
  doi={10.1109/IISWC53511.2021.00014}
}
```

## Getting Started and Documentation

For more information, see our [Getting Started page][33].


## Acknowledgements

The ILLIXR project started in [Sarita Adve’s research group][9],
    co-led by PhD candidate Muhammad Huzaifa, at the University of Illinois at Urbana-Champaign.
Other major contributors include
    Rishi Desai,
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
The external libraries and softwares included in ILLIXR each have their own licenses and
    must be used according to those licenses:

-   [ElasticFusion][14] \ [ElasticFusion license][15]

-   [KinectFusion][40] \ [MIT License][46]

-   [GTSAM][27] \ [Simplified BSD License][28]

-   [HOTlab][20] \ [GNU Lesser General Public License v3.0][21]

-   [libspatialaudio][18] \ [GNU Lesser General Public License v2.1][19]

-   [Monado][22] \ [Boost Software License 1.0][23]

-   [moodycamel::ConcurrentQueue][31] \ [Simplified BSD License][32]

-   [Open-VINS][12] \ [GNU General Public License v3.0][13]

-   [RITnet][16] \ [MIT License][17]

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
[8]:    https://ieeexplore.ieee.org/abstract/document/9668280
[9]:    http://rsim.cs.illinois.edu
[10]:   https://monado.dev
[11]:   https://research.fb.com/programs/openeds-challenge
[12]:   https://github.com/rpng/open_vins
[13]:   https://www.gnu.org/licenses/gpl-3.0.html
[14]:   https://github.com/mp3guy/ElasticFusion
[15]:   https://github.com/mp3guy/ElasticFusion/blob/master/LICENSE.txt
[16]:   https://github.com/AayushKrChaudhary/RITnet
[17]:   https://github.com/AayushKrChaudhary/RITnet/blob/master/License.md
[18]:   https://github.com/videolabs/libspatialaudio
[19]:   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
[20]:   https://github.com/MartinPersson/HOTlab
[21]:   https://www.gnu.org/licenses/lgpl-3.0.html
[22]:   https://gitlab.freedesktop.org/monado/monado
[23]:   https://choosealicense.com/licenses/bsl-1.0
[24]:   https://gitter.im/ILLIXR/community
[25]:   https://github.com/ILLIXR/ILLIXR/releases
[26]:   https://www.khronos.org/openxr
[27]:   https://github.com/ILLIXR/gtsam
[28]:   https://github.com/borglab/gtsam/blob/develop/LICENSE.BSD
[31]:   https://github.com/cameron314/concurrentqueue
[32]:   https://github.com/cameron314/concurrentqueue/blob/master/LICENSE.md
[33]:   https://illixr.github.io/ILLIXR/getting_started/
[34]:   https://illixr.github.io/ILLIXR/LICENSE/
[35]:   https://illixr.github.io/ILLIXR/illixr_plugins/
[36]:   https://illixr.github.io/ILLIXR/writing_your_plugin/
[37]:   http://illixr.org
[38]:   https://github.com/ILLIXR/ILLIXR
[39]:   https://github.com/ILLIXR/ILLIXR/releases/tag/v3.1.0
[40]:   https://github.com/ILLIXR/KinectFusionApp/tree/illixr-integration
[41]:   https://github.com/ILLIXR/ILLIXR/tree/master/realsense
[42]:   https://www.stereolabs.com/zed-mini/
[43]:   https://youtu.be/ZY98lWksnpM
[44]:   https://ws.engr.illinois.edu/sitemanager/getfile.asp?id=2971
[45]:   https://youtu.be/GVcCW8WgEDY
[46]:   https://github.com/chrdiller/KinectFusionApp/blob/master/LICENSE.txt
[47]:   https://discord.gg/upkvy7x3W4
[48]:   mailto:lists@lists.cs.illinois.edu?subject=sub%20illixr-community
[49]:   mailto:illixr@cs.illinois.edu
[50]:   https://illixr.org/open_meetings
