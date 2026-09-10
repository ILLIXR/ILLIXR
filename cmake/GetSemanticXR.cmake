set(SEMANTIC_XR_DIR "${CMAKE_SOURCE_DIR}")
fetch_git(NAME SemanticXR
          REPO https://github.com/ILLIXR/SemanticXR.git
          TAG ab17a9eec06446af5703c12adb11552c883c6f7e
          OVERRIDE_BUILD ""
          SRC_DIR ${SEMANTIC_XR_DIR}/python
)

FetchContent_MakeAvailable(SemanticXR)
message(STATUS "SemanticXR available at ${semanticxr_SOURCE_DIR}")
