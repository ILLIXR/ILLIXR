# Computational Holography

Part of [ILLIXR][10], the Illinios Extended Reality Benchmark Suite.
This component is responsible for calculating image holograms (per-pixel phase masks) using
    the Weighted Gerchberg–Saxton (GSW) algorithm.

# Files

## generateHologram.<span></span>cu

### generateHologram

Host side kernel launch code.

### propagateToSpotPositions

CUDA kernel that propagates phases from the SLM plane to the depth plane using Fresnel summation.

### propagateToSpotSum

CUDA kernel that sums up the per-thread block results from the propagateToSpotPositions() kernel.

### propagateToSLM

CUDA kernel that calculates the error function at the depth planes and updates the SLM phases.

## goldenHologram.<span></span>cu

The original hologram implementation. This implementation did not support arbitrary SLM sizes
    and colored holograms.

# Installation & Usage

Under `C/source/`

    make all
    make jetson

`make all` compiles for the SM75 architecture, while `make jetson` compiles for SM70.
To run this code on a older NVIDIA GPU, please change the SM architecture accordingly.

To run our modified hologram code:

    ./hologram

To run the original hologram code:

    ./goldenHologram

# License

This code is available under the LGPL license.


[//]: # (- Internal -)

[10]:   index.md
