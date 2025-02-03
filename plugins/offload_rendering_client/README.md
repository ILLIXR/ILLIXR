# offload_rendering_client
## Summary
`offload_rendering_client` receives encoded frames from the network, and uses a mix of NPPI and FFMPEG to decode the frames before updating the buffer pool. Note that there is a known color shift issue (to be fixed), where the decoded frame's colors are slightly different from the original frame (likely due to the many conversions between YUV and RGBA).