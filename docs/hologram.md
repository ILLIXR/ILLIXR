## Hologram

`hologram` , computational holography, subscribes to a`timewarp_gl` output signal indicating a timewarped frame, which means hologram should start processing. Hologram takes depth buffer of the frame to calculate a Spatial Light Modulator input. For more details of hologram, please refer to our paper. Since we do not have an accurate prediction of vsync, hologram process is one frame delayed. 

Currently this component is for profiling purpose only. Performance-wise, hologram is input invariant. Current illixr and monado do not support depth buffer from user application. Algorithms to calculate depth spots from depth buffer is also missing.
