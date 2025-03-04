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

The images can be constructed with the provided script `docksr.sh` which takes the following options:
```
  -a : make all possible images (exclusive with all other flags)
  -f : docker image will have everything (exclusive with all other flags)
  -g : docker image will have no GPL licensed code (exclusive with -a and -f)
  -h : print this help and exit
  -l : docker image will have no LGPL or GPL licensed code, implies -g (exclusive with -a and -f)
  -z : docker image will not have the ZED SDK (exclusive with -a and -f)
```

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

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full">docker.sh -f</pre>
</div>

**Docker image**: illixr/illixr_full:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full_dl">docker pull ghcr.io/illixr/illixr_full:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full_run">docker run --gpus all -it --privileged illixr_full:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_full_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_full_comp">export ILLIXR_DOCKER_IMAGE=full
docker compose -f docker-compose.yaml up</pre>
</div>

---

### No GPL

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL">docker.sh -g</pre>
</div>

**Docker image**: illixr/illixr_no_gpl:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_dl">docker pull ghcr.io/illixr/illixr_no_gpl:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_run">docker run --gpus all -it --privileged illixr_no_gpl:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_comp">export ILLIXR_DOCKER_IMAGE=no_gpl
docker compose -f docker-compose.yaml up</pre>
</div>

---

### No LGPL

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL">docker.sh -l</pre>
</div>

**Docker image**: illixr/illixr_no_lgpl:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_dl">docker pull ghcr.io/illixr/illixr_no_lgpl:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_run">docker run --gpus all -it --privileged illixr_no_lgpl:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_comp">export ILLIXR_DOCKER_IMAGE=no_lgpl
docker compose -f docker-compose.yaml up</pre>
</div>

---

### No Zed

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED">docker.sh -z</pre>
</div>

**Docker image**: illixr/illixr_no_zed:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED_dl">docker pull ghcr.io/illixr/illixr_no_zed:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED_run">docker run --gpus all -it --privileged illixr_no_zed:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noZED_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noZED_comp">export ILLIXR_DOCKER_IMAGE=no_zed
docker compose -f docker-compose.yaml up</pre>
</div>

---

### No GPL or ZED

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED">docker.sh -gz</pre>
</div>

**Docker image**: illixr/illixr_no_gpl_zed:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED_dl">docker pull ghcr.io/illixr/illixr_no_gpl_zed:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED_run">docker run --gpus all -it --privileged illixr_no_gpl_zed:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noGPL_ZED_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noGPL_ZED_comp">export ILLIXR_DOCKER_IMAGE=no_gpl_zed
docker compose -f docker-compose.yaml up</pre>
</div>

---

### No LGPL or ZED

**Build**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED">docker.sh -lz</pre>
</div>

**Docker image**: illixr/illixr_no_lgpl_zed:latest

**Download**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED_dl" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED_dl">docker pull ghcr.io/illixr/illixr_no_lgpl_zed:latest</pre>
</div>

**Run**:
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED_run" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED_run">docker run --gpus all -it --privileged illixr_no_lgpl_zed:latest</pre>
</div>

**Docker Compose**
<div class="code-box-copy">
<button class="code-box-copy__btn" data-clipboard-target="#dockerfile_noLGPL_ZED_comp" title="Copy"></button>
<pre class="language-shell" id="dockerfile_noLGPL_ZED_comp">export ILLIXR_DOCKER_IMAGE=no_lgpl_zed
docker compose -f docker-compose.yaml up</pre>
</div>


[^1]: A common issue with running the NVIDIA images produces the error `Failed to initialize NVML: Unknown Error`. A solution to this can be found [here][10]



[//]: # (- References -)
[1]:  https://docker.com
[2]:  https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html
[10]: https://forums.developer.nvidia.com/t/nvida-container-toolkit-failed-to-initialize-nvml-unknown-error/286219
