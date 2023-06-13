find_package(eCAL QUIET)

if(eCAL_FOUND)
    set(eCAL_VERSION "${eCAL_VERSION}" PARENT_SCOPE)
else()
    EXTERNALPROJECT_ADD(eCAL_ext
            GIT_REPOSITORY https://github.com/eclipse-ecal/ecal.git
            GIT_TAG df71cc5a7aea31e963a40be9b0989b1aa9490d87
            PREFIX ${CMAKE_BINARY_DIR}/_deps/eCAL
            UPDATE_COMMAND git submodule update --init
            PATCH_COMMAND patch -p0 < ${PROJECT_SOURCE_DIR}/cmake/eCAL.patch
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DECAL_THIRDPARTY_BUILD_PROTOBUF=OFF -DECAL_THIRDPARTY_BUILD_CURL=OFF -DECAL_THIRDPARTY_BUILD_HDF5=OFF -DECAL_THIRDPARTY_BUILD_QWT=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib
            )
    set(eCAL_DEP_STR "eCAL_ext")
    set(eCAL_EXTERNAL Yes)
    set(eCAL_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(eCAL_LIBRARIES ecal_core;ecal_core_pb)
endif()

