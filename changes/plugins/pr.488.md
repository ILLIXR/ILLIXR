---
- author.astro-friedel
- author.Madhuparna04
---
This work introduces the new android_data plugin. This plugin captures imu and image data from an Android device
and writes the data to the internal storage. The android_data plugin uses pure Android functions calls and has no 
reliance on OpenXR. This plugin is the Android equivalent to the record_imu_cam plugin, with the difference that it
captures only monocular images.
