file(GLOB HELIX_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(
    SRCS ${HELIX_SRCS}
    INCLUDE_DIRS "src"
)
# assembly.h only has a portable C path under the ARDUINO guard
target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-implicit-fallthrough -Wno-maybe-uninitialized)
