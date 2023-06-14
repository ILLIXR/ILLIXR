# CMake module to look for eCAL
# if it is not found then it is downloaded and marked for compilation and install

find_package(eCAL QUIET)

if(eCAL_FOUND)
    set(eCAL_VERSION "${eCAL_VERSION}" PARENT_SCOPE)   # set current version
else()
    EXTERNALPROJECT_ADD(eCAL_ext
            GIT_REPOSITORY https://github.com/eclipse-ecal/ecal.git   # Git repo for source code
            GIT_TAG df71cc5a7aea31e963a40be9b0989b1aa9490d87          # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/eCAL                     # the build directory
            UPDATE_COMMAND git submodule update --init
            # the code needs to be patched to build on some systems
            PATCH_COMMAND ${PROJECT_SOURCE_DIR}/cmake/do_patch.sh -p ${PROJECT_SOURCE_DIR}/cmake/eCAL.patch
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DECAL_THIRDPARTY_BUILD_PROTOBUF=OFF -DECAL_THIRDPARTY_BUILD_CURL=OFF -DECAL_THIRDPARTY_BUILD_HDF5=OFF -DECAL_THIRDPARTY_BUILD_QWT=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib
            )
    # set variables for use by modules that depend on this one
    set(eCAL_DEP_STR "eCAL_ext")   # Dependency string for other modules that depend on this one
    set(eCAL_EXTERNAL Yes)         # Mark that this module is being built
    set(eCAL_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(eCAL_LIBRARIES ecal_core;ecal_core_pb)
endif()

