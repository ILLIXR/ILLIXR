---
- author.astro-friedel
---
Deployed a new UDP based networking plugin. It can be used in parallel with the existing TCP based plugin, or by itself. Each networked topic can choose its own method. This does introduce **breaking** changes, due to updates to the core networking headers. This plugin works on Windows and Linux.