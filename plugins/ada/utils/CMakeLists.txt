add_library(encode_decode_common OBJECT
            ${CMAKE_CURRENT_LIST_DIR}/common.hpp
            ${CMAKE_CURRENT_LIST_DIR}/common.cpp
)

target_include_directories(encode_decode_common PUBLIC
                           ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(encode_decode_common PUBLIC
                      ${OpenCV_LIBS}
)

add_library(encode_utils OBJECT
            ${CMAKE_CURRENT_LIST_DIR}/encode_utils.hpp
            ${CMAKE_CURRENT_LIST_DIR}/encode_utils.cpp
            $<TARGET_OBJECTS:encode_decode_common>
)

target_include_directories(encode_utils PUBLIC
                           ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(encode_utils PUBLIC
                      ${OpenCV_LIBS}
)

add_library(decode_utils OBJECT
            ${CMAKE_CURRENT_LIST_DIR}/decode_utils.hpp
            ${CMAKE_CURRENT_LIST_DIR}/decode_utils.cpp
            $<TARGET_OBJECTS:encode_decode_common>
)

target_include_directories(decode_utils PUBLIC
                           ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(decode_utils PUBLIC
                      ${OpenCV_LIBS}
)
