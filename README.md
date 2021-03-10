# ILLIXR

[![NCSA licensed](https://img.shields.io/badge/license-NCSA-blue.svg)](LICENSE)
[![CI](https://github.com/ILLIXR/ILLIXR/workflows/illixr-tests-master/badge.svg)](https://github.com/ILLIXR/ILLIXR/actions)
[![Gitter chat](https://badges.gitter.im/gitterHQ/gitter.png)](https://gitter.im/ILLIXR/community)

<a href="https://www.youtube.com/watch?v=5GXsUP9_34U">
    <img
        alt="ILLIXR Simple Demo"
        src="https://img.youtube.com/vi/5GXsUP9_34U/0.jpg"
        style="width: 480px"
    >
</a>

Illinois Extended Reality testbed or ILLIXR (pronounced like elixir) is
    the first open-source full-system Extended Reality (XR) testbed.
It contains standalone state-of-the-art components representative of a generic XR workflow,
    as well as a runtime framework that integrates these components into an XR system.
ILLIXR's runtime integration framework is modular, extensible, and [OpenXR][26]-compatible.

We use the term _components_ and not _kernels_ or _computations_ because
    each component of ILLIXR is an entire application in itself,
    and consists of many kernels and computations.
At the moment, ILLIXR interfaces the following state-of-the-art components,
    with periodic ILLIXR source snapshots hosted on our [releases page][25]:

-   Perception
    1.  [Eye tracking][3] **
    1.  [Scene reconstruction][2] **
    1.  [Simultaneous Localization and Mapping][1] **

-   Visual
    1.  [Chromatic aberration correction][5]
    1.  [Computational holography for adaptive multi-focal displays][6] **
    1.  [Lens distortion correction][5]
    1.  [Reprojection][5]

-   Aural
    1.  [Audio encoding][4]
    1.  [Audio playback][4]

(** Source is hosted in an external repository under the [ILLIXR project][7].)

We plan on adding more components (e.g., graphics and multiple versions for individual components)
    and extending the runtime in the future.
Our goal is not to create a commercial quality XR product for current hardware.
Instead, the goal for ILLIXR is to advance computer architecture, systems, and
    hardware-software co-design research for XR by making available a full system and
    key state-of-the-art components of both modern and future XR applications.

Many of the current components of ILLIXR were developed by domain experts and
    obtained from publicly available repositories.
They were modified for one or more of the following reasons:
    fixing compilation,
    adding features,
    removing extraneous code/dependencies.
Each component not developed by us is available as a forked github repository for
    proper attribution to its authors.

Detailed descriptions of each component and our runtime,
    including performance and energy profiles, can be found in
    our [paper][8].


## Publications

We request that you cite our following paper (new version coming soon) when you
    use ILLIXR for a publication.
We would also appreciate it if you send us a citation once your work has been published.

```
@misc{HuzaifaDesai2020,
    title={Exploring Extended Reality with ILLIXR: A new Playground for Architecture Research},
    author={Muhammad Huzaifa and Rishi Desai and Xutao Jiang and Joseph Ravichandran and Finn Sinclair and Sarita V. Adve},
    year={2020},
    eprint={2004.04643},
    primaryClass={cs.DC}
}
```


## Getting Started and Documentation

For more information, see our [Getting Started page][33].


## Acknowledgements

Muhammad Huzaifa led the development of ILLIXR in [Sarita Adve’s research group][9]
    at the University of Illinois at Urbana-Champaign.
Other major contributors include
    Rishi Desai,
    Samuel Grayson,
    Xutao Jiang,
    Ying Jing,
    Jae Lee,
    Fang Lu,
    Yihan Pang,
    Joseph Ravichandran,
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

-   [GTSAM][27] \ [Simplified BSD License][28]

-   [HOTlab][20] \ [GNU Lesser General Public License v3.0][21]

-   [Kimera-VIO][29] \ [Simplified BSD License][30]

-   [libspatialaudio][18] \ [GNU Lesser General Public License v2.1][19]

-   [Monado][22] \ [Boost Software License 1.0][23]

-   [moodycamel::ConcurrentQueue][31] \ [Simplified BSD License][32]

-   [Open-VINS][12] \ [GNU General Public License v3.0][13]

-   [RITnet][16] \ [MIT License][17]

Note that ILLIXR's extensibility allows the source to be configured and compiled using only
    permissively licensed software.
For more details on configuring ILLIXR, see the [ILLIXR Plugins][35] and
    [Writing your Plugin][36] pages.


## Get in Touch

Whether you are a computer architect, a systems person, an XR application developer,
    or just anyone interested in XR, we would love to hear your feedback on ILLIXR!
ILLIXR is a living testbed and we would like to both refine existing components and add new ones. 
We believe ILLIXR has the opportunity to drive future computer architecture and
    systems research for XR,
    and can benefit from contributions from other researchers and organizations.
If you would like to be a part of this effort, please contact us at
    _illixr at cs dot illinois dot edu_ or visit us on [Gitter][24] or just send us a pull request!


[//]: # (- References -)

[1]:    https://github.com/ILLIXR/open_vins
[2]:    https://github.com/ILLIXR/ElasticFusion
[3]:    https://github.com/ILLIXR/RITnet
[4]:    https://github.com/ILLIXR/audio_pipeline
[5]:    https://github.com/ILLIXR/visual_postprocessing
[6]:    https://github.com/ILLIXR/HOTlab
[7]:    https://github.com/ILLIXR
[8]:    https://arxiv.org/pdf/2004.04643.pdf
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
[29]:   https://github.com/ILLIXR/Kimera-VIO
[30]:   https://github.com/MIT-SPARK/Kimera-VIO/blob/master/LICENSE.BSD
[31]:   https://github.com/cameron314/concurrentqueue
[32]:   https://github.com/cameron314/concurrentqueue/blob/master/LICENSE.md
[33]:   https://illixr.github.io/ILLIXR/getting_started/
[34]:   https://illixr.github.io/ILLIXR/LICENSE/
[35]:   https://illixr.github.io/ILLIXR/illixr_plugins/
[36]:   https://illixr.github.io/ILLIXR/writing_your_plugin/
