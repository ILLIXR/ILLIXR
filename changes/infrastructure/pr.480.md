---
- author.astro-friedel
---
Large **breaking** update to the headers for poses. With the use of poses for things like hand tracking and gesture detection, holding most of the poses in a single header was becoming difficult. The changes in this work break that header into several heirarchical, and smaller headers. Additionally, the boost serialization code was similarly refactored.