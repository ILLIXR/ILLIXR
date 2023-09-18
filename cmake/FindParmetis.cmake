include(FindPackageHandleStandardArgs)

if(DEFINED ENV{PARMETIS_INCLUDE_DIR})
    set(PARMETIS_INCLUDE_DIR "$ENV{PARMETIS_INCLUDE_DIR}")
endif()

find_path(PARMETIS_INC_DIR "parmetis.h"
        HINTS ${PARMETIS_INCLUDE_DIR}
        )

if(DEFINED ENV{PARMETIS_LIB_DIR})
    set(PARMETIS_LIB_DIR "$ENV{PARMETIS_LIB_DIR}")
endif()

set(PARMETIS_LIB_NAME parmetis PARMETIS_LIB)
find_library(PARMETIS_LIBRARY
        NAMES ${PARMETIS_LIB_NAME}
        PATHS ${PARMETIS_LIB_DIR}
        )

find_package_handle_standard_args(Parmetis DEFAULT_MSG
        PARMETIS_INC_DIR
        PARMETIS_LIBRARY
        )

if(Parmetis_FOUND)
    set(Parmetis_INCLUDE_DIRS ${PARMETIS_INC_DIR})
    set(Parmetis_LIBRARIES ${PARMETIS_LIBRARY})
    message(STATUS "Searching for libparmetis... ok")
else()
    message(FATAL_ERROR "Searching for libparmetis... failed")
endif()