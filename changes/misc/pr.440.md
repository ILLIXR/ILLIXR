---
- author.astro-friedel
---
Calls to `abort()` have been removed from the code base and replaced with exceptions. Uses of `#ifdef NDEBUG` which surrounded only logging calls have also been removed from the code base, letting the logger be available regardless of how the code was compiled. 
