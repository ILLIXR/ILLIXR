# Display Backends

ILLIXR supports multiple display backends for rendering output. Each backend is designed for different use cases and can
be configured through environment variables.

## Available Display Backends

ILLIXR supports three display backends:

1. **GLFW** (Default)
    - Standard windowed display using GLFW
    - Best for desktop development and debugging
    - Provides a resizable window interface

2. **X11 Direct Mode**
    - Direct display mode using X11
    - Provides lower latency by bypassing the window manager
    - Useful for tethered headsets directly connected to the GPU using DisplayPort or HDMI

3. **Headless**
    - No display output
    - Useful for testing and benchmarking
    - Can be used in environments without display hardware

## Configuration

The display backend and its behavior can be configured using the following environment variables:

### ILLIXR_DISPLAY_MODE

Controls which display backend to use.

Possible values:

- `glfw` (Default) - Use GLFW windowed mode
- `x11_direct` - Use X11 direct mode
- `headless` - Use headless mode

Example:

``` bash
export ILLIXR_DISPLAY_MODE=glfw
```

### ILLIXR_VULKAN_SELECT_GPU

Allows manual selection of the GPU device when multiple are available. By default, the first available GPU is used. No
need to set this if you only have one GPU.

- Value: Integer index of the GPU for Vulkan to use (0-based)
- Default: -1 (automatic selection of first suitable device)

The available GPUs and their indices will be printed during startup.

Example:

``` bash
export ILLIXR_VULKAN_SELECT_GPU=1 # Select the second GPU
```

### ILLIXR_DIRECT_MODE_DISPLAY

Required when using X11 direct mode (`ILLIXR_DISPLAY_MODE=x11_direct`). Specifies which display to use. No need to set
this if you only have one display output connected to the GPU.

!!! note

    Currently, this backend is only tested with NVIDIA GPUs.

- Value: Integer index of the display to use (0-based)
- Must be set when using X11 direct mode
- Available displays and their indices will be printed during startup

Example:

``` bash
export ILLIXR_DIRECT_MODE_DISPLAY=0  # Use the first display
```

## Display Selection Process

1. The system will enumerate available displays and GPUs during startup
2. For GPU selection:
    - If `ILLIXR_VULKAN_SELECT_GPU` is not set, the first suitable GPU is selected
    - If set, the specified GPU index is used
    - The system will print available GPUs and their capabilities

3. For X11 direct mode:
    - The system will list available displays
    - `ILLIXR_DIRECT_MODE_DISPLAY` must be set to a valid display index
    - The selected display will be acquired for direct mode access
    - The highest refresh rate mode will be automatically selected
