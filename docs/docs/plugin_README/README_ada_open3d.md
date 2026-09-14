# Ada reconstruction with Open3D

Ada can reconstruct the scene with either InfiniTAM or Open3D on the CUDA server.
The device, mesh compression, chunk transport, decoder, and scene manager are
shared. Choose one reconstruction plugin in the server configuration:

```yaml
# InfiniTAM: the existing ada_server profile
plugins: tcp_network_backend,ada.server_rx,ada.server_tx,ada.infinitam,ada.mesh_compression
```

```yaml
# Open3D: the ada_server_open3d profile
plugins: tcp_network_backend,ada.server_rx,ada.server_tx,ada.open3d,ada.mesh_compression
```

Loading both reconstruction plugins is an error. Both profiles are generated from
`plugins/plugins.yaml`, default to eight workers and chunks, extract every 15
frames, and disable ILLIXR structured recording. Copy the dataset and network
settings from your existing server configuration. Use the same `ada_device`
configuration for either backend; see the [Ada setup guide](README_ada.md).

## Build the local Open3D dependency

This backend requires the selective extraction extension in the independent
Open3D checkout. Stock Open3D 0.19.0 does not provide that API. The CMake check
rejects a stock installation. InfiniTAM builds do not require Open3D.

The initial local dependency is:

| Setting | Value |
| --- | --- |
| Checkout | `/home/yihanp2/Research/SecondPaper/Open3D-Ada` |
| Upstream | `https://github.com/isl-org/Open3D.git` |
| Upstream tag | `v0.19.0` |
| Upstream revision | `1e7b17438687a0b0c1e5a7187321ac7044afe275` |
| Local branch | `ada-incremental` |
| Local revision | `25860def88d8662563cf0f5859f6bacbce92a835` |

The local revision has not been published. Publishing the separate fork and
pinning an ILLIXR organization URL is a later step. Until then, obtain this
checkout or its Git bundle and build it locally; cloning the upstream tag alone
is insufficient. Keep build directories and comparison artifacts outside both
source repositories.

The validated server build used GCC 9, CMake 3.27.9, CUDA 12.1, and an RTX 4090
with architecture 89, inside the existing DeepStream 6.3 environment. The required
build options and dependency choices were:

```bash
cmake -S /path/to/Open3D-Ada -B /path/to/open3d-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/open3d-install \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_CUDA_MODULE=ON \
  -DCMAKE_CUDA_ARCHITECTURES=89 \
  -DBUILD_WITH_CUDA_STATIC=OFF \
  -DGLIBCXX_USE_CXX11_ABI=ON \
  -DBUILD_GUI=OFF -DBUILD_WEBRTC=OFF \
  -DBUILD_PYTHON_MODULE=OFF -DBUILD_EXAMPLES=OFF -DBUILD_UNIT_TESTS=OFF \
  -DBUILD_ISPC_MODULE=OFF -DWITH_IPP=OFF \
  -DUSE_SYSTEM_EIGEN3=ON -DUSE_SYSTEM_GLFW=OFF \
  -DUSE_SYSTEM_GLEW=ON -DUSE_SYSTEM_CURL=ON \
  -DUSE_BLAS=ON -DUSE_SYSTEM_BLAS=ON
cmake --build /path/to/open3d-build --parallel 10 --target install
```

Use your server GPU's CUDA architecture instead of 89 when appropriate. The
validated build used system OpenBLAS/LAPACKE and NASM, and Open3D's bundled GLFW;
Ubuntu 20.04's system GLFW lacks APIs needed by Open3D 0.19.0. The build still
includes Open3D's legacy visualization library, but Ada does not create a renderer.

Add Open3D to your existing ILLIXR server build:

```bash
cmake -S /path/to/ILLIXR -B /path/to/illixr-build \
  -DCMAKE_INSTALL_PREFIX=/path/to/illixr-install \
  -DOpen3D_DIR=/path/to/open3d-install/lib/cmake/Open3D \
  -DUSE_ADA.OPEN3D=ON \
  -DUSE_TCP_NETWORK_BACKEND=ON \
  -DUSE_ADA.SERVER_RX=ON -DUSE_ADA.SERVER_TX=ON \
  -DUSE_ADA.MESH_COMPRESSION=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/illixr-build --parallel 8
cmake --install /path/to/illixr-build
export LD_LIBRARY_PATH=/path/to/open3d-install/lib:/path/to/illixr-install/lib:$LD_LIBRARY_PATH
```

Keep `USE_ADA.INFINITAM=ON` as well if both backend libraries should be available
in that build. Only the plugin selected in the runtime YAML is loaded. Build
Open3D and ILLIXR with the same C++ standard-library ABI. The Open3D headers and
ILLIXR headers are isolated in different translation units because the projects
use different fmt versions.

The comparison used DeepStream 6.3 on the server and the approved DeepStream 7.1
installation on the JetPack 6 device. The device's Wi-Fi power-saving check stayed
enabled and confirmed power saving was off throughout each run.

## Reconstruction and incremental updates

Open3D uses a CUDA tensor `VoxelBlockGrid` with Float32 TSDF and weight attributes,
2 cm voxels, 8 × 8 × 8 blocks, and a 10 cm truncation distance. Its initial
10,000-block hash capacity grows through Open3D's hash map as needed.

The plugin reads the same affine depth calibration as InfiniTAM and integrates
the supplied camera poses without running ICP. It converts the incoming UInt16
depth to calibrated meters, excludes zero samples and depths outside
`[0.2, 4.0)` meters, and inverts the camera-to-world pose for Open3D. Integration
and extraction are serialized for each volume.

After integration, each changed block and its seven possible predecessor owners
are marked dirty. A marching-cubes cell belongs to the block containing its
lower corner, so changing a boundary voxel can also change a predecessor's
surface. Coordinates, rather than hash-buffer indices, survive hash-map growth.

`VoxelBlockGrid::ExtractTriangleMeshForBlocks(block_coords, weight_threshold)`
accepts Int32 block coordinates on the volume's device and returns Float32
positions, Int32 triangle indices, and an Int32 `(faces, 3)` triangle attribute
named `block_coords`. It reads neighbors from the complete volume, deduplicates
selected keys, and ignores keys that are not allocated. The original
`ExtractTriangleMesh()` API and implementation remain the full-volume reference.
The selective API emits a triangle soup with the same interpolation and winding;
it does not produce colors or normals.

Extraction first counts triangles, then writes them into retained buffers. The
buffers grow geometrically to meet the actual requirement. A previous returned
mesh retains ownership of its storage; the extractor allocates another buffer
instead of overwriting a result still in use. Ada retains its CPU view while
constructing all independently owned chunk meshes.

Every extraction sends its full dirty-owner replacement list, including owners
that now emit no triangles. Each of the eight chunks then carries its own exact
block dictionary in the existing Draco payload. No additional dictionary message
or shared dictionary lifetime is introduced. Scene IDs, chunk IDs, and immediate
independent chunk delivery retain Ada's existing semantics. Empty chunks use a
valid attribute-free sequential Draco mesh and still complete the update.

Open3D extracts positions in meters. The chunk builder converts them to Ada's
existing centimeters using Float32 arithmetic and preserves triangle winding.
Compression retains 14-bit explicit position quantization, origin `(0, 0, 0)`,
range 2,000 cm, generic setting 8, and speed settings `(3, 3)`. Block IDs and
dictionary coordinates remain exact integers. The existing device formatter and
scene manager continue to use Float32 vertex positions.

### Remaining differences from InfiniTAM

Matching voxel size, truncation, input depth, and poses does not make the two
fusion algorithms identical. Open3D retains Float32 TSDF values and accumulating
Float32 weights; the current InfiniTAM configuration stores a signed 16-bit TSDF
and caps depth weight at 100 while continuing fusion. They also differ in block
activation, image-boundary handling, and the extraction validity rules.

Open3D uses its original default extraction threshold of 3: all eight corners
must have weight **greater than 3**. InfiniTAM's current mesher checks allocation
and rejects corners with TSDF equal to +1. It does not apply Open3D's weight rule.
These differences can change surface coverage, triangle counts, and vertex
positions. Correctness of the adapter is assessed against full Open3D extraction;
agreement with InfiniTAM is a separate reconstruction-quality comparison.

## Export the final scene-management mesh

Set this value in the device YAML for an Open3D run:

```yaml
env_vars:
  ADA_FINAL_MESH_PATH: /path/to/comparison/open3d_final_scene.obj
```

For the matching InfiniTAM run, use `infinitam_final_scene.obj`. Merge the setting
into your existing `env_vars` map. The exporter runs at the existing final update
condition, after the scene manager records `Ready`; export time is excluded from
that timestamp. It writes the last fully completed extraction, with no extra
extraction for leftover frames. With `FRAME_COUNT=1158` and `FPS=15`, that is
scene 76 from input frame 1155, after all 77 scheduled updates.

The OBJ files use Ada's world coordinates and **centimeter units**: one stored
unit equals 0.01 meters. No alignment, normalization, axis flip, or scaling is
applied during export. Nine significant digits preserve Float32 values. The
exporter omits nullified placeholder faces and unreferenced vertex storage,
remaps only the exported indices, and preserves the live scene and its winding.
Without `ADA_FINAL_MESH_PATH`, the existing `76.obj` naming behavior is retained.

For comparisons, record the backend and library revisions, dataset/calibration/
pose hashes, frame range, final scene ID, worker counts, extraction interval,
codec settings, units, vertex and triangle counts, and final ready timestamp in
an external manifest. Compare the incremental Open3D assembly with its unchanged
full extractor before compression, and validate received geometry separately.
Distinguish controlled computation measurements from live Wi-Fi measurements;
cross-machine latency also needs clock-offset uncertainty.
