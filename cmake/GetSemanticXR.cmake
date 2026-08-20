set(SEMANTIC_XR_DIR "${CMAKE_SOURCE_DIR}")
fetch_git(NAME SemanticXR
          REPO https://github.com/ILLIXR/SemanticXR.git
          TAG c959b9fce15d337b16cd7680b74c520446f7a3a0
          OVERRIDE_BUILD ""
          SRC_DIR ${SEMANTIC_XR_DIR}/python
)
