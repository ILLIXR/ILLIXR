# Ada Setup and Run Guide

Thank you for your interest in **Ada** :
[Ada: A Distributed, Power-Aware, Real-Time Scene Provider for XR](https://rsim.cs.illinois.edu/Pubs/25-TVCG-Ada.pdf).

This guide provides step-by-step instructions to set up and run the Ada system within the [ILLIXR](https://illixr.org) testbed, using the ScanNet dataset.

---

## 1) Prerequiste 
Hardware:
Device: Jetson Orin AGX
  - Note: using a different device can make Ada not produce the correct mesh
  - Why Ada uses GSTREAMER which uses NVIDIA's DeepStream to use NVENC/NVDEC to use efficient depth encoding. However, the needed flag in Ada ("enable-lossless" for nvv4l2h265enc/nvv4l2h264enc element in GStreamer) can be missing in some combination. IN theory any NVIDIA GPU that has architecture Ampere or above (30xx series and Orin above) supports this but the software support seems still not fully there 
  - If you want to test on a machine that does not support this capability, we plan to release a version using previous work's encoding method (16bit Depth to HSV color model to 8-bit RBG) that has nexts best ability to perserve depth as a future release. 
Server: Any Server with a NVIDIA GPU (Ada's number is measured on a 3080TI, but we have also verified on using Jetson Orin AGX)


## 1) Install Dependencies

### Core ILLIXR Dependencies
Follow the official [ILLIXR getting started guide](https://illixr.github.io/ILLIXR/getting_started/) to install all core dependencies.
- ADA was originally built on branch `offload-h265-clean`, commit `cc4d7e8`.
- We are in the process of updating the code to the latest `main` branch.

### Draco Library (custom)
Build and install Draco using the **custom repository** provided with ADA (do **not** use the upstream Google repo directly).
- Reference for general build steps: [Google Draco BUILDING.md](https://github.com/google/draco/blob/main/BUILDING.md).

### DeepStream & GStreamer
Install NVIDIA DeepStream and GStreamer.
- Official docs: [NVIDIA DeepStream Dev Guide](https://docs.nvidia.com/metropolis/deepstream/dev-guide/)
- Archived reference (used during our installation):  
  [Wayback Machine link](http://web.archive.org/web/20230327195958/https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_Quickstart.html#dgpu-setup-for-ubuntu)

---

## 2) Prepare the ScanNet Dataset

### Step 1: Download
Follow the official instructions: [ScanNet download](https://github.com/ScanNet/ScanNet#scannet-data).

### Step 2: Clone ILLIXR's ScanNet Fork
```bash
git clone git@github.com:ILLIXR/ScanNet.git
cd ScanNet
git checkout infinitam
cd SensReader/c++
make
```

### Step 3: Create a Sequence Directory
Create the final sequence directory (outside the repo):
```
scene<sceneId>/
├─ images   # depth and color images
└─ poses    # associated pose files
```

### Step 4: Convert `.sens` Files
Follow the SensReader instructions to extract calibration, pose, color, and depth:
- https://github.com/ILLIXR/ScanNet/tree/master/SensReader/c%2B%2B

Example:
```bash
./sens /path/to/scannet/scans/scene0000_00/scene0000_00.sens        /path/to/scenes/scene0000/images
```

### Step 5: Convert to InfiniTAM Format
```bash
cd InfiniTAM/scripts/misc
python3.8 convert_scannet.py --source-dir /path/to/scenes/scene<sceneId>/images
```

---

## 3) Repository Structure

- **device_side_plugins/** → Device-side plugins (requires the prepared ScanNet data).
- **server_side_plugins/** → Server-side plugins.
- **draco_custom/** → Custom Draco source (required on both device and server).

---

## 4) Device Setup (Jetson Orin)

### 4.1 Configure Power Mode (nvpmodel)
> **Note:** Make a backup of `/etc/nvpmodel.conf` before editing. You’ll need `sudo`.

Add a power model for ADA (example with `ID=6` and `NAME=Ada`):
```conf
< POWER_MODEL ID=6 NAME=Ada >
CPU_ONLINE CORE_0 1
CPU_ONLINE CORE_1 1
CPU_ONLINE CORE_2 1
CPU_ONLINE CORE_3 1
CPU_ONLINE CORE_4 1
CPU_ONLINE CORE_5 1
CPU_ONLINE CORE_6 1
CPU_ONLINE CORE_7 1
CPU_ONLINE CORE_8 0
CPU_ONLINE CORE_9 0
CPU_ONLINE CORE_10 0
CPU_ONLINE CORE_11 0
TPC_POWER_GATING TPC_PG_MASK 254
GPU_POWER_CONTROL_ENABLE GPU_PWR_CNTL_EN on
CPU_A78_0 MIN_FREQ 1728000
CPU_A78_0 MAX_FREQ 1728000
CPU_A78_1 MIN_FREQ 1728000
CPU_A78_1 MAX_FREQ 1728000
GPU MIN_FREQ 714000000
GPU MAX_FREQ 714000000
GPU_POWER_CONTROL_DISABLE GPU_PWR_CNTL_DIS auto
EMC MAX_FREQ 2133000000
DLA0_CORE MAX_FREQ 1369600000
DLA1_CORE MAX_FREQ 1369600000
DLA0_FALCON MAX_FREQ 729600000
DLA1_FALCON MAX_FREQ 729600000
PVA0_VPS MAX_FREQ 512000000
PVA0_AXI MAX_FREQ 358400000
```

- DLA/PVA are not used by ADA in our setup; their values are not critical.
- After saving the file, either:
  - **GUI:** Click the NVIDIA logo (top-right), choose **Power Mode → Ada**, then reboot when prompted.
  - **CLI (alternative):**
    ```bash
    sudo nvpmodel -m 6
    sudo reboot
    ```

### 4.2 Set IPs and Ports
Edit `common/network/net_config.hpp` on **both** device and server so they agree:
```cpp
const std::string SERVER_IP   = "your.server.ip";
const std::string SERVER_PORT_1 = "port1";
const std::string SERVER_PORT_2 = "port2";

const std::string DEVICE_IP   = "your.device.ip";
const std::string DEVICE_PORT_1 = "port1";
const std::string DEVICE_PORT_2 = "port2";
```
> Ensure ports do not conflict with other services and firewall rules allow traffic in both directions.

---

## 5) Running ADA

### On the Device
```bash
export PARTIAL_MESH_COUNT=X   # e.g., 8 for testing
export frame_count_=Y          # total frames in the sequence (e.g., 1158 for ScanNet scene 0005)
export fps_=Z                  # proactive scene extraction frequency (paper setup: 15)
./runner.sh config/release_device.yaml
```

### On the Server
```bash
export frame_count_=Y  # must match the device
export fps_=Z          # must match the device
./runner.sh config/release_server.yaml
```

---

## 6) Adjusting Compression Parallelism

To increase compression parallelism (e.g., from 8 to 10 threads):

1. **Update environment variables** to reflect the desired thread count.
2. **Update YAML configs** to include the extra plugins:
   - In `config/release_server.yaml`: add `mesh_compression_8`, `mesh_compression_9`.
   - In `config/release_device.yaml`: add `mesh_decompression_8`, `mesh_decompression_9`.
3. **Modify server-side `InfiniTAM/plugin.cpp`** and uncomment the indicated lines:
   - (near lines **57–58**)
     ```cpp
     //, _m_mesh_8{switchboard_->get_writer<mesh_type>("requested_scene_8")}
     //, _m_mesh_9{switchboard_->get_writer<mesh_type>("requested_scene_9")}
     ```
   - (near lines **248–251**)
     ```cpp
     // [&](std::unique_ptr<draco::PlyReader>&& ply_reader, unsigned face_number, unsigned per_vertices, unsigned num_partitions) {
     //     _m_mesh_8.put(_m_mesh_8.allocate<mesh_type>(mesh_type{std::move(ply_reader), scene_id, 8, num_partitions, face_number, per_vertices, set_active}));},
     // [&](std::unique_ptr<draco::PlyReader>&& ply_reader, unsigned face_number, unsigned per_vertices, unsigned num_partitions) {
     //     _m_mesh_9.put(_m_mesh_9.allocate<mesh_type>(mesh_type{std::move(ply_reader), scene_id, 9, num_partitions, face_number, per_vertices, set_active}));},
     ```
   - (near lines **424–425**)
     ```cpp
     //switchboard::writer<mesh_type> _m_mesh_8;
     //switchboard::writer<mesh_type> _m_mesh_9;
     ```
4. **Modify device-side `device_rx/plugin.cpp`** and uncomment the indicated lines:
   - (near lines **44–45**)
     ```cpp
     //, _m_mesh_8{switchboard_->get_writer<mesh_type>("compressed_scene_8")}
     //, _m_mesh_9{switchboard_->get_writer<mesh_type>("compressed_scene_9")}
     ```
   - (near lines **147–148**)
     ```cpp
     //case 8: _m_mesh_8.put(_m_mesh_8.allocate<mesh_type>(mesh_type{payload, false, scene_id, sr_output.chunk_id(), sr_output.max_chunk()})); break;
     //case 9: _m_mesh_9.put(_m_mesh_9.allocate<mesh_type>(mesh_type{payload, false, scene_id, sr_output.chunk_id(), sr_output.max_chunk()})); break;
     ```
   - (near lines **198–199**)
     ```cpp
     //switchboard::writer<mesh_type> _m_mesh_8;
     //switchboard::writer<mesh_type> _m_mesh_9;
     ```

> **Tip:** Keep `PARTIAL_MESH_COUNT` consistent with the highest chunk index you enable. For example, if you enable up to `_m_mesh_9`, set `PARTIAL_MESH_COUNT=10`.

---

## 7) Tested Configurations

### ILLIXR
- Branch: `offload-h265-clean`
- Commit: `cc4d7e8`

### Jetson Orin (Device)
- JetPack: 5.1.3
- DeepStream: 6.3
- CUDA: 11.4

### Server
- Clang: 10.0.0
- CUDA: 11.4 (12.1 also works)
- DeepStream: 6.3
