# ILLIXR

ILLIXR (pronounced like elixir) is an open-source Extended Reality (XR) benchmark suite. It contains several core state-of-the-art components of a generic XR pipeline, components that are required in most, if not all, XR applications. We use the term _components_ and not _kernels_ or _computations_ because each component of ILLIXR is an entire application in itself, and consists of many kernels and computations. At the moment, ILLIXR contains the following state-of-the-art components, all of which are included as sub-modules in this repo.

1. [Simultaneous Localization and Mapping](https://github.com/ILLIXR/open_vins)
2. [Scene reconstruction](https://github.com/ILLIXR/ElasticFusion)
3. [Ambisonic encoding](https://github.com/ILLIXR/audio_pipeline)
4. [Ambisonic manipulation and binauralization](https://github.com/ILLIXR/audio_pipeline)
5. [Lens distortion correction](https://github.com/ILLIXR/visual_postprocessing)
6. [Chromatic aberration correction](https://github.com/ILLIXR/visual_postprocessing)
7. [Time warp](https://github.com/ILLIXR/visual_postprocessing)
8. [Computational holography for adaptive multi-focal displays](https://github.com/ILLIXR/HOTlab)
9. [Eye-tracking](https://github.com/ILLIXR/RITnet)

We plan on adding more components to ILLIXR (e.g., graphics and multiple versions for individual components), including a runtime to integrate all of the components into a full XR system. Our goal is not to create a commercial quality XR product for current hardware. Instead, the goal for ILLIXR is to advance computer architecture, systems, and hardware-software co-design research for XR by making available key state-of-the-art components of both modern and future XR applications. 

Many of the current components of ILLIXR were developed by domain experts and obtained from publicly available repositories. They were modified for one or more of the following reasons: fixing compilation, adding features, or removing extraneous code or dependencies. Each component not developed by us is available as a forked github repository for proper attribution to its authors.

Detailed descriptions of each component, including performance and energy profiles, can be found in our [paper](http://rsim.cs.illinois.edu/Pubs/illixr.pdf).

# Publications

We request that you cite our following paper when you use ILLIXR for a publication. We would also appreciate it if you send us a citation once your work has been published.

- Muhammad Huzaifa, Rishi Desai, Xutao Jiang, Joseph Ravichandran, Finn Sinclair, Sarita V. Adve, [Exploring Extended Reality with ILLIXR: A New Playground for Architecture Research](http://rsim.cs.illinois.edu/Pubs/illixr.pdf).


# Setup

Each component of ILLIXR is packaged as its own repository for modularity. Please refer to the setup instructions of each individual component in `benchmark/`.

To clone this repo use the following command:
```
git clone https://github.com/ILLIXR/ILLIXR.git --recursive
```
# Acknowledgements

Muhammad Huzaifa led the development of ILLIXR in [Sarita Adve’s research group](http://rsim.cs.illinois.edu/) at the University of Illinois at Urbana-Champaign. Other major contributors include Rishi Desai, Xutao Jiang, Joseph Ravichandran, and Finn Sinclair.

ILLIXR came together after many consultations with researchers and practitioners in many domains: audio, graphics, optics, robotics, signal processing, and extended reality systems. We are deeply grateful for all of these discussions and specifically to the following: Wei Cu, Aleksandra Faust, Liang Gao, Matt Horsnell, Amit Jindal, Steve LaValle, Steve Lovegrove, Andrew Maimone, Vegard &#216;ye, Martin Persson, Archontis Politis, Eric Shaffer, Paris Smaragdis, and Chris Widdowson.

The development of ILLIXR was supported by the Applications Driving Architectures (ADA) Research Center, a JUMP Center co-sponsored by SRC and DARPA, the Center for Future Architectures Research (C-FAR), one of the six centers of STARnet, a Semiconductor Research Corporation program sponsored by MARCO and DARPA, and by a Google Faculty Research Award. The development of ILLIXR was also aided by generous hardware and software donations from ARM and NVIDIA.

Wesley Darvin came up with the name for ILLIXR. Abdulrahman Mahmoud helped with the design of this website.


# Licensing Structure

ILLIXR is available as open-source software under the [University of Illinois/NCSA Open Source License](https://github.com/ILLIXR/illixr.github.io/blob/master/LICENSE). As mentioned above, ILLIXR largely consists of components developed by domain experts and modified for the purposes of inclusion in ILLIXR. However, ILLIXR does contain software developed solely by us. **The NCSA license is limited to only this software**. The external libraries and softwares included in ILLIXR each have their own licenses and must be used according to those licenses:

- [Open-VINS](https://github.com/rpng/open_vins) - [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html)
- [ElasticFusion](https://github.com/mp3guy/ElasticFusion) - [ElasticFusion license](https://github.com/mp3guy/ElasticFusion/blob/master/LICENSE.txt)
- [libspatialaudio](https://github.com/videolabs/libspatialaudio) - [GNU Lesser General Public License v2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)
- [HOTlab](https://github.com/MartinPersson/HOTlab) - [GNU Lesser General Public License v3.0](https://www.gnu.org/licenses/lgpl-3.0.html)
- [RITnet](https://github.com/ILLIXR/RITnet) - [MIT License](https://github.com/AayushKrChaudhary/RITnet/blob/master/License.md)


# Get In Touch

Whether you are a computer architect, a systems person, an XR application developer, or just anyone interested in XR, we would love to hear your feedback on ILLIXR! ILLIXR is a living benchmark suite and we would like to both refine existing components and add new ones. We believe ILLIXR has the opportunity to drive future computer architecture and systems research for XR, and can benefit from contributions from other researchers and organizations. If you would like to be a part of this effort, please contact us at _illixr at cs dot illinois dot edu_
