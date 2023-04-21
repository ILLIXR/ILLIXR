if(USE_V4_CODE)
    find_package(GTSAM 4.3.0 EXACT QUIET)
    if(GTSAM_FOUND)
        set(GTSAM_EXTERNAL No)
        set(GTSAM_LIBRARIES "gtsam")
    else()
        set(GTSAM_EXTERNAL Yes)
        EXTERNALPROJECT_ADD(GTSAM
                GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git
                GIT_TAG 9a7d05459a88c27c65b93ea75b68fa1bc0fc0e4b
                PREFIX ${CMAKE_BINARY_DIR}/_deps/GTSAM
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DGTSAM_WITH_TBB=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_POSE3_EXPMAP=ON -DGTSAM_ROT3_EXPMAP=ON -DGTSAM_WITH_EIGEN_UNSUPPORTED=ON -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF
                INSTALL_COMMAND make install && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsamRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam.so && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstableRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstable.so)
        set(GTSAM_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
        EXTERNALPROJECT_GET_PROPERTY(GTSAM install_dir)
        add_library(gtsam SHARED IMPORTED)
        set(GTSAM_DEP_STR "GTSAM")
        set_property(TARGET gtsam PROPERTY IMPORTED_LOCATION ${install_dir}/src/GTSAM-build/gtsam/libgtsam${CMAKE_BUILD_TYPE}.so)
        add_dependencies(gtsam GTSAM)
    endif()
else()
    find_package(GTSAM 3.2 QUIET)
    if(GTSAM_FOUND)
        set(GTSAM_EXTERNAL No)
        set(GTSAM_LIBRARIES "gtsam")
    else()
        set(GTSAM_EXTERNAL Yes)
        EXTERNALPROJECT_ADD(GTSAM
                GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git
                GIT_TAG 3.2.3
                PREFIX ${CMAKE_BINARY_DIR}/_deps/GTSAM
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DGTSAM_WITH_TBB=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_POSE3_EXPMAP=ON -DGTSAM_ROT3_EXPMAP=ON -DGTSAM_WITH_EIGEN_UNSUPPORTED=ON -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF
                INSTALL_COMMAND make install && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsamRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam.so && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstableRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstable.so)
        set(GTSAM_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
        EXTERNALPROJECT_GET_PROPERTY(GTSAM install_dir)
        add_library(gtsam SHARED IMPORTED)
        set(GTSAM_DEP_STR "GTSAM")
        set_property(TARGET gtsam PROPERTY IMPORTED_LOCATION ${install_dir}/src/GTSAM-build/gtsam/libgtsam${CMAKE_BUILD_TYPE}.so)
        add_dependencies(gtsam GTSAM)
    endif()
endif()