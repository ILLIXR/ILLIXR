# Using Third Party Code

ILLIXR is an open-source project that uses third-party code and libraries. This document outlines the process and
licensing constraints for using these inside ILLIXR.

## Precompiled Libraries

### Provided by OS Package Managers

If the third party library being used is provided by a package manager (yum, dnf, apt, etc.) then there are no licensing
issues, as it is not being provided by ILLIXR. Any library that is used this way needs to be added to the
`docs/docs/modules.json` file. See the [documentation][I11] for more information on how to do this.

To include the library in the build, add the library to the appropriate `CMakeLists.txt` file. If the package (or CMake
itself) provides a `Find<package>.cmake` or `<package>Config.cmake` file, then use

``` cmake
find_package(<package> REQUIRED)
``` 

to ensure the library exists on the build system. If the `*.cmake` file is in a non-standard location you may need to
update the [`CMAKE_PREFIX_PATH`][E12] variable to include the directory containing the `*.cmake` file.

``` cmake
set(CMAKE_PREFIX_PATH "/path/to/file ${CMAKE_PREFIX_PATH}")
```

If the package does not have a `Find<package>.cmake` file, but provides a `pkg-config` (<package>.pc) file, then use

``` cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(<package> REQUIRED <package>)
```

to ensure the library exists on the build system. If the `*.pc` file is in a non-standard location you may need to
update the `PKG_CONFIG_PATH` environment variable to include the directory containing the `*.pc` file.

``` cmake
set(ENV{PKG_CONFIG_PATH} "/path/to/file:$ENV{PKG_CONFIG_PATH}")
```

If neither of these are provided you may need to use the [`find_library`][E14] command to locate the library on the
build system.

### Provided by Third Party

If the precompiled library is provided as a download or via a third-party installer, then the library must be licensed
under a compatible license. See the [license][I10] documentation for more information. To use a third-party installer in
ILLIXR, you will need to add directives in the appropriate `CMakeLists.txt` file to download and install the library.
Commands such as [`file(DOWNLOAD ...)`][E16], [`execute_process(COMMAND ...)`][E17], and [`add_custom_command`][E18] can
be used to accomplish this. You will need to be aware that CMake will not automatically associate the installation of
the library with its use in your code. You will likely need to add targets to the `DEPENDS` list of your code's CMake
target.

## Built From Source

Any third-party code that is built from source must be licensed under a compatible license. See the [license][I10]
documentation for more information. To use this code in ILLIXR it is recommended to create a `cmake/Get<package>.cmake`
file that will download and build the library. See files in the `cmake` directory for examples. Then, in the appropriate
`CMakeLists.txt` file for the code that depends on the library, use either the `get_external_for_plugin` (if your code
is a plugin) or `get_external` macros to include the library in the build. Also, be sure to add the library to the
`DEPENDS` list of your code's CMake target.

## Linking to the Library

Once the library has been made available to the build system (see the above sections), you will need to ensure that the
system can find any relevant headers and link to and locate the library at build and runtime. This is accomplished with
the [`target_include_directories`][E19] and [`target_link_libraries`][E20] commands in the appropriate `CMakeLists.txt`
file. See any of the existing `CMakeLists.txt` files in `plugins/*` for examples.

## Helpful CMake Links

- [CMAKE_PREFIX_PATH][E12] The path that CMake will search for `Find<package>.cmake` files.
- [find_package][E13] The CMake command to search the system for the given package and populate variables with the
  results.
- [find_library][E14] The CMake command to search for a library on the system.
- [Using pkg-config with CMake][E15]
- [file][E16] The CMake command for numerous file operations.
- [execute_process][E17] The CMake command to execute a process.
- [add_custom_command][E18] The CMake command to add a custom command to the build system.
- [target_include_directories][E19] The CMake command to add include directories to a target.
- [target_link_libraries][E20] The CMake command to add libraries to link to, to a target.

[//]: # (- Internal -)

[I10]: licenses.md

[I11]: ../working_with/modules.md


[//]: # (- external -)

[E12]: https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html

[E13]: https://cmake.org/cmake/help/latest/command/find_package.html

[E14]: https://cmake.org/cmake/help/latest/command/find_library.html

[E15]: https://cmake.org/cmake/help/latest/module/FindPkgConfig.html

[E16]: https://cmake.org/cmake/help/latest/command/file.html#download

[E17]: https://cmake.org/cmake/help/latest/command/execute_process.html

[E18]: https://cmake.org/cmake/help/latest/command/add_custom_command.html

[E19]: https://cmake.org/cmake/help/latest/command/target_include_directories.html

[E20]: https://cmake.org/cmake/help/latest/command/target_link_libraries.html
