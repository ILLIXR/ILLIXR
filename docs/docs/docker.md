# Docker Files and Images

With each release we provide a set of Dockerfiles and pre-built Docker images. The Dockerfiles can be used to build, and modify, your own image, while the images can be used directly to use ILLIXR. These images derive from NVIDIA cuda based images, using the latest Ubuntu LTS release.

## Prerequisites

The following must be installed for these images to be used:

1. [Docker][1], you must have an installed and running Docker engine
2. [NVIDIA Container Toolkit][2], provides access to the local NVIDIA GPU from the container.[^1]

Alternately the pre-built images can be downloaded from . There are several versions available depending on your needs:

## Versions

We provide several versions of ILLIXR, depending on your needs:

| Name                              | Description                                                                                                            |
|:----------------------------------|:-----------------------------------------------------------------------------------------------------------------------|
| [Full](#full-build)               | Build all plugins and dependencies.                                                                                    |
| [No GPL](#no-gpl)                 | Build all plugins and dependencies which are not covered by a GPL license.                                             |
| [No LGPL](#no-lgpl)               | Build all plugins and dependencies which are not covered by a LGPL or GPL license.                                     |
| [No ZED](#no-zed)                 | Build all plugins and dependencies except the ZEDS related ones.                                                       |
| [No GPL or ZED](#no-gpl-or-zed)   | Build all plugins and dependencies which are not covered by a GPL license, also do not build ZED related code.         |
| [No LGPL or ZED](#no-lgpl-or-zed) | Build all plugins and dependencies which are not covered by a LGPL or GPL license, also do not build ZED related code. |

In all versions:

 - the ILLIXR code are `git clone`d into `/home/illixr/ILLIXR`
 - the main user is named `illixr` with no password
 - the `illixr` user has `sudo` privileges
 - any binaries (executables) compiled and installed by the build process are installed in `/home/illixr/bin`
 - any libraries compiled and installed by the build process are installed in `/home/illixr/lib`
 - `/home/illixr/bin` is already in the user's `$PATH`
 - `/home/illixr/lib` is already in the user's `$LD_LIBRARY_PATH`
 - you can start ILLIXR as normal `main.dbg.exe ....`

!!! note

  The use of the `--privileged` flag to `docker run` is recommended for our containers as it gives the container access to all devices on the host machine, allowing for easy use of usb cameras and other devices.

---

### Full Build

**Docker file**: Dockerfile.full

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full">docker build -t illixr:3.3_full -f Dockerfile.full .</pre>
</div>

**Docker image**: illixr_v3.3_full

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full_dl">docker pull ghcr.io/illixr/illixr_v3.3_full</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full_run">docker run --gpus all -it --privileged illixr_v3.3_full</pre>
</div>

---

### No GPL

**Docker file**: Dockerfile.noGPL

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL">docker build -t illixr:3.3_noGPL Dockerfile.noGPL</pre>
</div>

**Docker image**: illixr_v3.3_noGPL

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_dl">docker pull ghcr.io/illixr/illixr_v3.3_noGPL</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_run">docker run --gpus all -it --privileged illixr_v3.3_noGPL</pre>
</div>

---

### No LGPL

**Docker file**: Dockerfile.noLGPL

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL">docker build -t illixr:3.3_noLGPL Dockerfile.noLGPL</pre>
</div>

**Docker image**: illixr_v3.3_noLGPL

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_dl">docker pull ghcr.io/illixr/illixr_v3.3_noLGPL</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_run">docker run --gpus all -it --privileged illixr_v3.3_noLGPL</pre>
</div>

---

### No Zed

**Docker file**: Dockerfile.noZED

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED">docker build -t illixr:3.3_noZED Dockerfile.noZED</pre>
</div>

**Docker image**: illixr_v3.3_noZED

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED_dl">docker pull ghcr.io/illixr/illixr_v3.3_noZED</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED_run">docker run --gpus all -it --privileged illixr_v3.3_noZED</pre>
</div>

---

### No GPL or ZED

**Docker file**: Dockerfile.noGPL_ZED
**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED">docker build -t illixr:3.3_noGPL_ZED Dockerfile.noGPL_ZED</pre>
</div>

**Docker image**: illixr_v3.3_noGPL_ZED

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED_dl">docker pull ghcr.io/illixr/illixr_v3.3_noGPL_ZED</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED_run">docker run --gpus all -it --privileged illixr_v3.3_noGPL_ZED</pre>
</div>

---

### No LGPL or ZED

**Docker file**: Dockerfile.noLGPL_ZED

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED">docker build -t illixr:3.3_noLGPL_ZED Dockerfile.noLGPL_ZED</pre>
</div>

**Docker image**: illixr_v3.3_noLGPL_ZED

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED_dl">docker pull ghcr.io/illixr/illixr_v3.3_noLGPL_ZED</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED_run">docker run --gpus all -it --privileged illixr_v3.3_noLGPL_ZED</pre>
</div>


[^1]: A common issue with running the NVIDIA images produces the error `Failed to initialize NVML: Unknown Error`. A solution to this can be found [here][10]



[//]: # (- References -)
[1]:  https://docker.com
[2]:  https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html
[10]: https://forums.developer.nvidia.com/t/nvida-container-toolkit-failed-to-initialize-nvml-unknown-error/286219
