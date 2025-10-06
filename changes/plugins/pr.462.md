---
- author.jianxiapyh
---
This is the first open-source release of **Ada**, implemented on top of the [ILLIXR](https://illixr.org) testbed.  
Ada provides a reproducible implementation of our ISMAR/TVCG 2025 [paper](https://rsim.cs.illinois.edu/Pubs/25-TVCG-Ada.pdf) results.
Its features include:
- Full implementation of **Ada’s distributed scene provisioning pipeline**:
    - Efficient depth encoding with MSB (lossless) + LSB (tunable bitrate)
    - Decoupled volumetric fusion and scene extraction
    - Extraction-aware scene management
    - Real-time mesh compression with parallel encoding
- **Device-side and server-side plugin set** for ILLIXR
- Support for ScanNet dataset and InfiniTAM-based reconstruction
- Example prepared ScanNet sequence for quick testing
