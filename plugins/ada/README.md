# Ada Setup and Run Guide

Thank you for your interest in **Ada** :
[Ada: A Distributed, Power-Aware, Real-Time Scene Provider for XR](https://rsim.cs.illinois.edu/Pubs/25-TVCG-Ada.pdf).

This guide provides step-by-step instructions to set up and run the Ada system within the [ILLIXR](https://illixr.org) testbed, using the ScanNet dataset.

---

## 1) Installation
Before building Ada, make sure the following dependencies are installed:
- **ILLIXR:** latest `main` branch  
- **Jetson Orin (device):**  
  - JetPack ≥ 5.1.3  
  - DeepStream ≥ 6.3  
  - CUDA ≥ 11.4  
- **Server:**  
  - Clang ≥ 10.0.0  
  - CUDA ≥ 11.4  
  - DeepStream ≥ 6.3  

> We recommend JetPack 6.0.0 / DeepStream 7.1 / CUDA 12.2 (or JetPack 5.1.3 / DeepStream 6.3 / CUDA 11.4), which were used in our tested configurations.

### 1.1 Build and Install Ada Components in ILLIXR

#### Step 1: Clone ILLIXR
```bash
git clone git@github.com:ILLIXR/ILLIXR.git
cd ILLIXR
```

#### Step 2 Configure the Build
Create a build directory and run cmake with Ada components enabled.

Replace /path/to/install with the installation directory of your choice.

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_INSTALL_PREFIX=/path/to/install \
  -DBUILD_SHARED_LIBS=ON \
  -DUSE_ADA.OFFLINE_SCANNET=ON \
  -DUSE_TCP_NETWORK_BACKEND=ON \
  -DUSE_ADA.DEVICE_TX=ON \
  -DUSE_ADA.DEVICE_RX=ON \
  -DUSE_ADA.SERVER_RX=ON \
  -DUSE_ADA.SERVER_TX=ON \
  -DUSE_ADA.INFINITAM=ON \
  -DUSE_ADA.MESH_COMPRESSION=ON \
  -DUSE_ADA.MESH_DECOMPRESSION_GREY=ON \
  -DUSE_ADA.SCENE_MANAGEMENT=ON \
  -DCMAKE_BUILD_TYPE=Release
```

### Step 3: Build 
```bash
cmake --build . -j8
```
(Adjust -j8 based on the number of cores on your machine.)

### Step 4: Install
```bash
cmake --install .
```

### Step 5: Update Environment Variables
Add the install folder’s lib directory to your LD_LIBRARY_PATH:
```bash
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
```
(Add the above line to your .bashrc or .zshrc for persistence across sessions.)



## 2) Setting up Ada
To run Ada, you’ll need a device with Jetson-class hardware and a server with an NVIDIA GPU.  
Below are the configurations we used for reproducibility in the Ada paper.

**Hardware**
- **Device (required):** NVIDIA Jetson Orin AGX  
  (Ada relies on Jetson’s NVENC/NVDEC capabilities for depth encoding.)  
- **Server (flexible):** Any machine with an NVIDIA GPU  
  (we used an RTX 3080 Ti for our experiments, but other GPUs also work).


## 3) ILLIXR Configuration
Ada runs as a set of ILLIXR plugins. To launch it, you need to provide configuration files that specify  
which plugins to load, where to find the dataset, and Ada specific tuning parameters.  

### Example Device Configuration File
```yaml
plugins: ada.offline_scannet,tcp_network_backend,ada.device_tx,ada.device_rx,ada.mesh_decompression_grey,ada.scene_management


install_prefix: /path/to/install #location of your ILLIXR build
env_vars:
  ILLIXR_RUN_DURATION: 1200 #how long you want to run ILLIXR (in seconds)
  DATA: /home/illixr/Downloads/scannet_0005 #location of your dataset
  ILLIXR_DATA: /home/illixr/Downloads/scannet_0005
  FRAME_COUNT: 1158 #frames in your dataset
  FPS: 15 #how often you want to trigger proactive scene extraction (Sec 4.2 in the paper)
  PARTIAL_MESH_COUNT: 8 #number of parallel compression and decompression of mesh happening (Sec 4.4 in the paper)
  MESH_COMPRESS_PARALLELISM: 8 #should match PARTIAL_MESH_COUNT
  ILLIXR_TCP_SERVER_IP:  127.0.0.1 #IP address of the server (can be localhost if testing on one machine)
  ILLIXR_TCP_SERVER_PORT: 9000 #Port of the server (your choice )
  ILLIXR_TCP_CLIENT_IP: 127.0.0.1 #IP address of the device (can be localhost if testing on one machine)
  ILLIXR_TCP_CLIENT_PORT: 9001 #Port of the device (your choice, should be different from server port)
  ILLIXR_IS_CLIENT: 1 #1 for device, 0 for server
  ENABLE_OFFLOAD: false #ILLIXR-related flags, not used in Ada, keep them false
  ENABLE_ALIGNMENT: false
  ENABLE_VERBOSE_ERRORS: false
  ENABLE_PRE_SLEEP: false
```

### Example Server Configuration File
```yaml
plugins: tcp_network_backend,ada.server_rx,ada.server_tx,ada.infinitam,ada.mesh_compression

install_prefix: /path/to/install #location of your ILLIXR build
env_vars:
  ILLIXR_RUN_DURATION: 1200 #how long you want to run ILLIXR (in seconds)
  DATA: /home/illixr/Downloads/scannet_0005 #location of your dataset
  ILLIXR_DATA: /home/illixr/Downloads/scannet_0005
  FRAME_COUNT: 1158 #frames in your dataset
  FPS: 15 #how often you want to trigger proactive scene extraction (Sec 4.2 in the paper)
  PARTIAL_MESH_COUNT: 8 #number of parallel compression and decompression of mesh happening (Sec 4.4 in the paper)
  MESH_COMPRESS_PARALLELISM: 8 #should match PARTIAL_MESH_COUNT
  ILLIXR_TCP_SERVER_IP:  127.0.0.1 #IP address of the server (can be localhost if testing on one machine)
  ILLIXR_TCP_SERVER_PORT: 9000 #Port of the server (your choice )
  ILLIXR_TCP_CLIENT_IP: 127.0.0.1 #IP address of the device (can be localhost if testing on one machine)
  ILLIXR_TCP_CLIENT_PORT: 9001  #Port of the device (your choice, should be different from server port)
  ILLIXR_IS_CLIENT: 0 #1 for device, 0 for server
  ENABLE_OFFLOAD: false #ILLIXR-related flags, not used in Ada, keep them false
  ENABLE_ALIGNMENT: false
  ENABLE_VERBOSE_ERRORS: false
  ENABLE_PRE_SLEEP: false
```
#### What differs between device and server?

The plugin set (device loads offline_scannet, rx/tx, decompression, scene management; server loads rx/tx, InfiniTAM, compression).

The role flag: ILLIXR_IS_CLIENT = 1 (device) vs 0 (server).

### How to Understand Ada-Specific Parameters in YAML

- **FPS**  
  - Controls the **proactive scene extraction rate**.  
  - In our paper’s evaluation, proactive extraction was triggered every *N* frames (we used **every 15 frames**).  
  - ⚠️ *Note: this name may be confusing since it overlaps with dataset playback rate; we plan to update it in a future release.*

- **MESH_COMPRESS_PARALLELISM** and **PARTIAL_MESH_COUNT**  
  - `MESH_COMPRESS_PARALLELISM`: number of worker threads launched to compress/decompress mesh chunks in parallel.  
  - `PARTIAL_MESH_COUNT`: number of chunks the mesh is divided into; the scene management plugin expects this value.  
  - In the current version, these **must match**.  
  - Future support will allow mismatch — e.g., splitting into 8 chunks but only using 4 compression threads.


## 4) Running Ada
To run Ada, open **two terminals** (one for the server, one for the device).  
Make sure both shells have `LD_LIBRARY_PATH` set to include your ILLIXR build directory:  
### Step 1: Start the Server:
```bash
./main.opt.exe -y your_server_config.yaml
```

### Step 2: Start the Device:
```bash
./main.opt.exe -y your_device.config.yaml
```

### Output:
- If you enable the `VERIFY` flag in `plugins/ada/scene_management/plugin.cpp`, Ada will write out a reconstructed mesh at the last update as `x.obj` (`x = FRAME_COUNT/FPS - 1`)
- A `recorded_data` folder will be created inside your build directory. This folder contains diagnostic and intermediate data collected during the run

### FAQ: 

#### Q1: Can I use Ada on a different device than Jetson Orin?

Ada relies on GStreamer with NVIDIA’s DeepStream (NVENC/NVDEC) for efficient depth encoding.  
In particular, Ada requires the `enable-lossless` flag for the `nvv4l2h265enc` / `nvv4l2h264enc` GStreamer elements.  
This flag may be missing in some driver + device combinations.  

- In theory, any NVIDIA GPU with Ampere or newer architecture (30xx series or Jetson Orin and above) supports this capability.  
- However, software support is inconsistent across platforms.  
- For devices that do not support this, we plan to release an alternative version using a prior method (16-bit depth → HSV color model → 8-bit RGB), which offers the next-best depth preservation.

#### Q2: How to Change the Scene Fidelity
To adjust scene fidelity in **Ada**:
1. Go to 
`ILLIXR/build/_deps/infinitam_ext-src/ITMLib/Utils/ITMLibSettings.cpp `
2. Find line 55:
`sceneParams(0.1f, 100, 0.02f, 0.2f, 4.0f, false), // 2cm //pyh Ada used config`
    - The third parameter (0.02f in this example) controls the voxel size.
    - Smaller values → higher fidelity (e.g., 0.02f = 2 cm).
    - Larger values → lower fidelity (e.g., 0.04f = 4 cm, 0.06f = 6 cm).
3. Predefined configurations are available:
    - Line 53 → 6 cm voxel size
    - Line 54 → 4 cm voxel size
    - Line 55 → 2 cm voxel size (default in Ada)
4. After editing, rebuild ILLIXR:
  ```bash
  cmake --build . -j8
  cmake --install .
  ```
