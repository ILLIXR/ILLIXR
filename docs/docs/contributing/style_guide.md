## Programming Style Guide

Consistency is the most important. Following the existing style, formatting, and naming conventions of the file you are modifying and of the overall ILLIXR project. Failure to do so will result in a prolonged review process that has to focus on updating the superficial aspects of your code, rather than improving its functionality and performance. Below are some general guidelines to follow. We also use a pre-commit bot that runs after pushes to github. It enforces some general formatting rules, so don't be surprised if you see minor updates to your code.

### Directory Structure

Here is the basic directory structure used by ILLIXR (some files/directories have been omitted for brevity)

```bash
├── cmake
│   ├── ConfigurationSummary.cmake    # functions to generate a summary after a cmake configuration run
│   ├── *.patch                       # patch files for any 3rd party code
│   ├── do_patch.sh                   # script for applying the patches
│   ├── Find*.cmake                   # cmake files for locating packages that do not have one installed by a repo
│   ├── Get*.cmake                    # cmake files used to build 3rd party packages
│   └── HelperFunctions.cmake         # general helper functions
├── CMakeLists.txt                    # main cmake file for ILLIXR, edit with caution
├── CONTRIBUTORS                      # list of contributors to ILLIXR
├── docs
│   ├── docs
│   │   ├── contributing
│   │   │   └── *.md                  # markdown files containing documentation on how to contribute to ILLIXR
│   │   ├── css
│   │   │   └── *.css                 # css files used in the generated HTML documentation
│   │   ├── images
│   │   │   ├── *.png                 # images used in the generated documentation
│   │   │   └── *.svg
│   │   ├── js
│   │   │   └── *.js                  # javascript files used in the generated HTML documentation
│   │   ├── plugin_README
│   │   │   └── *.md                  # readme files for individual plugins
│   │   ├── policies
│   │   │   └── *.md                  # general policy documents
│   │   ├── getting_started.md.in     # processed into markdown during cmake documentation build
│   │   ├── modules.json              # json style file listing plugins and any 3rd party dependencies
│   │   └── *.md                      # general documentation
│   ├── doxygen
│   │   └── Doxyfile.in               # file to control doxygen, processed during cmake configuration
│   └── mkdocs
│       └── mkdocs.yaml.in            # file to control mkdocs, processed during cmake configuration
├── include
│   └── illixr
│       └── *                         # header files used my multiple plugins or the main binary
├── plugins
│   ├── <plugin_name>                 # each plugin has its own subdirectory
│   │   └── CMakeLists.txt            # if the plugin is from another repository, then only this file is needed
│   ├── <plugin_name>
│   │   ├── CMakeLists.txt            # cmake file which configures the build for this plugin
│   │   ├── *.hpp                     # header files, if any, for the plugin
│   │   └── plugin.cpp                # every plugin must have this file which contains the code for the plugin
│   └── plugins.yaml                  # yaml style file listing configurations and plugins
├── profiles
│   └── *.yaml                        # yaml style files for each profile, these are auto-generated
└── src
    ├── CMakeLists.txt                # cmake file for configuring the build of the main binary
    ├── *.hpp                         # header files for the main binary
    └── *.cpp                         # source files for the main binary

```

### File Naming

Illixr has adopted the following file naming conventions:

  - files and directories are all lower case (the exception to this rule is if a specific naming convention is expected by outside code, e.g. CMakeLists.txt, Doxyfile, etc.)
  - the `_` can be used as a word seperator
  - header files should have the `.hpp` suffix
  - code files should have the `.cpp` suffix
  - documentation files should have the `.md` suffix
  - any third party code used in ILLIXR is not subject to the above rules and can keep their original naming conventions

### Header Files

ILLIXR has adopted the `#pragma once` include guard for all header files.

### Includes

Header files used via the `#include` pre-processor directive should be at the top of the code and header files, and ordered in the followimg fashion:

  - header associated with this file (`cpp` files only, e.g. `xyz.cpp` might include `xyz.hpp`)
  - _blank line_
  - C system headers (those in angle brackets and ending in .h, e.g. `<stdlib.h>`)
  - _blank line_
  - C++ library headers (those in angle brackets, with no suffix, e.g. `<string>`)
  - _blank line_
  - third party includes (e.g. `vulkan/vulkan_core.h`)
  - _blank line_
  - any headers from inside the ILLIXR codebase

Not every file will have includes from every group, in fact most files will not. Any missing group can just be skipped.
Header files in each group should be listed in alphabetical order. Some headers will only be included under certain conditions, by using `#ifdef` statements. These headers, and their conditionals, should be added below all other header files. This is an example:

```C++
#include <stdlib.h>

#include <iostream>
#include <string>

#include <vulkan/vulkan_core.h>

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"

#ifdef ZED
#include "zed.h"
#endif
```

Comments may be added between the groups as long as there is a blank line after each grouping. `#define` and `#undef` directives can be used at any place inside, before, or after any header grouping as needed.

### Namespace

All ILLIXR code is inside of the ILLIXR namespace.