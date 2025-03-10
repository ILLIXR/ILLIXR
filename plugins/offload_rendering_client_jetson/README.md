# offload_rendering_client_jetson

## Summary
`offload_rendering_client_jetson` receives encoded frames from the network, and uses the Jetson's Multimedia API to decode frames before updating the buffer pool. Setting the environment variable ``ILLIXR_USE_DEPTH_IMAGES`` to a non-zero value indicates that depth images are being received, and should thus also be decoded.