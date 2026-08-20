# semantic_python

## Summary

This plugin provides a bridge between ILLIXR and a Python script and marshalls the running of the script. The Python 
interpreter is given handles to reader and writer functions so that any script it runs can directly connect to the 
[`switchboard`][G10] to read and write topics. As the plugin launches, it instantiates a Python interpreter in a thread,
loads the [`switchboard`][G10] interface functions into the interpreter, and launches the Python script. On teardown, it
shuts down the interpreter. 

!!! note

    Due to the way the interpreter is shut down, internal Python cleanup (e.g. `finally:` blocks) are not guaranteed to be called.

## Working Example

ILLIXR has a [SemnticXR][L01] python repository that provides a real-time semantic SLAM system with object detection, 
segmentation, and CLIP-based understanding for interactive 3D scene mapping and querying. This code can be used with the
`sematic_python` plugin to integrate it into ILLIXR.

## Using the plugin

This plugin requires a bit more setup than most, due to its interaction with Python. It uses two environment variables 
to launch the script

- `SEMANTIC_PYTHON_SCRIPT` - the full path to the Python script to launch, this is required
- `SEMANTIC_PYTHON_ARGS` - a comma separated list of command line arguments for the script, this is optional

The `SEMANTIC_PYTHON_ARGS` can handle both value (e.g., x=y) and flag-based arguments. If an argument takes a list
(x=a,b,c), you should use a different delimiter so they get parsed correctly by the plugin.

For example:

```bash
export SEMANTIC_PYTHON_SCRIPT="/home/abc/myscript.py"
export SEMANTIC_PYTHON_ARGS="config=configs/myconfig.yaml,stride=3,max_frames=100,save_objects"
```

would result in the interpreter launching:

```bash
/home/abc/myscript.py --config configs/myconfig.yaml --stride 3 --max_frames 100 --save_objects
```

This plugin writes to a networked topic, so the use of a networking plugin (e.g., `tcp_network_backend`) is required.
You will need to give the networking plugin some configuration details in environment variables:

- `ILLIXR_TCP_SERVER_IP` - IP address of the server
- `ILLIXR_TCP_SERVER_PORT` - Port number of the server
- `ILLIXR_TCP_CLIENT_IP` - IP address of the client headset.
- `ILLIXR_TCP_CLIENT_PORT` - Port number of the client headset.

!!! note

    Due to port restructions on Android based headset devices, you may need to use ports above 49152. In our tests we
    used ports 50057 and 50058

### Data format

In ILLIXR, the switchboard readers and writers usually publish `struct` data objects. In Python these are presented as
`dict`s. The `semantic_python` plugin converts between these formats in the background. For example, the `semantic_data`
C++ struct

```c++
struct semantic_data : switchboard::event {
    std::vector<uint8_t> image;
    int32_t frame_number;
    int32_t width;
    int32_t height;
    std::vector<uint8_t> depth;
    int32_t depth_width;
    int32_t depth_height;
    float depth_near_z;
    float intrinsics[4];
    float depth_intrinsics[4];
    float rgb_camera_pose[16];
    float depth_pose[16];
    float max_depth;
};
```

becomes (assuming the `struct` instance id named `mydata`)

```
{
    "image":              = numpy 1D array of mydata.image
    "frame_number":       = mydata.frame_number
    "image_width":        = mydata.width;
    "image_height":       = mydata.height;
    "depth":              = numpy 1D array of mydata.depth
    "depth_width":        = mydata.depth_width;
    "depth_height":       = mydata.depth_height;
    "depth_near_z":       = mydata.depth_near_z;
    "intrinsics":         = numpy 1x4 array of mydata.intrinsics
    "depth_intrinsics":   = numpy 1x4 array of mydata.depth_intrinsics
    "rgb_camera_pose":    = numpy 4x4 array of mydata.rgb_camera_pose
    "depth_pose":         = numpy 4x4 array of mydata.depth_pose
    "max_depth_m":        = mydata.max_depth
}
```

in Python.

### Python virtual environments

Pybind11 can have difficulty identifying the correct Python libraries if you use a virtual environment for your Python
(e.g., `venv`, `uv`, etc.). To aid pybind11 in finding the correct library, we recommend adding the following arguments
to your CMake call. Two of the arguments are given twice with different capitalization. This will cover the issue that 
different versions of CMake use different capitalization for the same item. Any warnings produced about one set of these
not being used can be ignored.

- `-DPYTHON_EXECUTABLE=&lt;path to your python binary&gt;` (e.g., `/home/abc/.venv/bin/python3`)
- `-DPython_EXECUTABLE=&lt;path to your python binary&gt;` (e.g., `/home/abc/.venv/bin/python3`)
- `-DPYTHON_ROOT_DIR=&lt;path to the virtual environment root&gt;` (e.g., `/home/abc/.venv`)
- `-DPython_ROOT_DIR=&lt;path to the virtual environment root&gt;` (e.g., `/home/abc/.venv`)
- `-Dpybind11_DIR=&lt;path to the pybind11 CMake config files>` (e.g., `/home/abc/.venv/lib/python3.12/site-packages/pybind11/share/cmake/pybind11`)

When running ILLIXR, we recommend using these four environment variables.

- `LD_LIBRARY_PATH` - prepend the path to your virtual environment's lib directory (e.g., `/home/abc/.local/share/uv/python/cpython-3.12.6-linux-x86_64-gnu/lib`)
- `VIRTUAL_ENV` - the path to your virtual environment's root folder, this may already be set (e.g., `/home/abc/.venv`)
- `PYTHONHOME` - the path to your virtual environment's home, this may already be set (e.g., `/home/abc/.local/share/uv/python/cpython-3.12.6-linux-x86_64-gnu`)
- `PYTHONPATH` - the path to your virtual environment's site-packages folder, this may already be set (e.g., `/home/abc/.venv/lib/python3.12/site-packages`)

We also recommend preloading the correct Python library at the very top of your python script. Any additional imports should
be below this snippet.

```python
import ctypes
import os
# required because my python is a venv, the system python interferes
_uv_python_lib = os.path.join(
    os.environ.get("PYTHONHOME", ""),
    "lib",
    "libpython3.12.so.1.0"
)
if os.path.exists(_uv_python_lib):
    print(f"preloading {_uv_python_lib} with RTLD_GLOBAL")
    ctypes.CDLL(_uv_python_lib, mode=ctypes.RTLD_GLOBAL)
else:
    print(f"WARNING: could not find {_uv_python_lib}")
```

In the above snippet, replace the library name (`libpython3.12.so.1.0`) with the correct one from your environment. The
name must be the full name of the library, not any of the symlinks (`libpython3.12.so.1.0` will work, but `libpython3.12.so`
will not.)

## Python script

The plugin injects three proxy objects into this script's globals, they are automatically available, no import is needed:

- `illixr_semantic_reader` - reads semantic_data frames
- `illixr_voice_reader` - reads voice_query objects
- `illixr_response_writer` - writes query_response results back to Unity

To read from the switchboard, call the `get()` method from each reader.

```python
while True:
    frame = illixr_semantic_reader.get()
    if frame is not None:
       PROCESS THE DATA

    query = illixr_voice_reader.get()
    if query is not None:
        PROCESS THE DATA
        
    time.sleep(0.001)
```

To write to the switchboard, call the `put()` method for the writer.

```python
query_text = "Where are my keys?"
query_id = 1

centroid = [1.0, 0.5, 2.0]
points = [
    # 8 corners of a 0.2m cube centred on dummy_centroid
    0.9, 0.4, 1.9, 1.1, 0.4, 1.9,
    0.9, 0.6, 1.9, 1.1, 0.6, 1.9,
    0.9, 0.4, 2.1, 1.1, 0.4, 2.1,
    0.9, 0.6, 2.1, 1.1, 0.6, 2.1,
]

illixr_response_writer.put(
    query_id=query_id,
    point_clouds=[{"points": points, "centroid": centroid}],
    colors=[0.8, 0.2, 0.2],
    server_latency=0.0,
    text_query=query_text,
)

```

[//]: # (- glossary -)

[G10]: ../glossary.md#switchboard

[L01]:  https://github.com/ILLIXR/SemanticXR/blob/illixr/integration
