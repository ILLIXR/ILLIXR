# Ada v1.0 Release Notes

**Ada: A Distributed, Power-Aware, Real-Time Scene Provider for XR**  
([Paper link](https://rsim.cs.illinois.edu/Pubs/25-TVCG-Ada.pdf))

This is the first open-source release of **Ada**, implemented on top of the [ILLIXR](https://illixr.org) testbed.  
Ada provides a reproducible implementation of our ISMAR/TVCG 2025 paper results.

---

## ✨ Features
- Full implementation of **Ada’s distributed scene provisioning pipeline**:
  - Efficient depth encoding with MSB (lossless) + LSB (tunable bitrate)
  - Decoupled volumetric fusion and scene extraction
  - Extraction-aware scene management
  - Real-time mesh compression with parallel encoding
- **Device-side and server-side plugin set** for ILLIXR
- Support for ScanNet dataset and InfiniTAM-based reconstruction
- Example prepared ScanNet sequence for quick testing

---

## 🔧 Installation & Setup
- Works with **Jetson Orin AGX (device)** and any **NVIDIA GPU server**  
- Verified on:
  - JetPack 5.1.3 / CUDA 11.4 / DeepStream 6.3
  - JetPack 6.0.0 / CUDA 12.2 / DeepStream 7.1 (device only; server requires 6.3)  
- Requires ILLIXR `main` branch

See [Setup Guide](./README.md) for step-by-step instructions.

---

## ⚠️ Known Issues
- **Server with DeepStream 7.1**: decoding does not fail but produces **incorrect images**.  
  Use **DeepStream 6.3** on the server for now.  


---

## 📂 Example Data
- A prepared ScanNet sequence is available here: [Google Drive link](https://drive.google.com/drive/folders/1f2Q8nUpKCoNLar3zy_NbKrvptDTzwQHZ?usp=drive_link)

---

## 🙏 Acknowledgments
This work is part of our ISMAR/TVCG 2025 paper. See the paper for full technical details.  
Thanks to the ILLIXR community for support and integration.
