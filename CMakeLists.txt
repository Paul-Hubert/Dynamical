cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 17)

project(dynamical)

add_subdirectory(3rdparty/FastNoiseSIMD)
add_subdirectory(3rdparty/GameNetworkingSockets)

include_directories("./src")
include_directories("./3rdparty")
include_directories("./extra")
file(GLOB_RECURSE sources ./src/main.cpp ./src/*.cpp ./src/*.h)

add_executable(dynamical ${sources})
target_compile_options(dynamical PRIVATE -Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -Wno-class-memaccess -Wno-unused-but-set-variable -Wno-reorder -Wno-missing-field-initializers -Wno-unused-function)

find_package(Vulkan REQUIRED)
target_link_libraries(dynamical PRIVATE Vulkan::Vulkan)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE SDL2::SDL2 SDL2::SDL2main)
include_directories(${SDL2_INCLUDE_DIRS})

target_link_libraries(dynamical PRIVATE FastNoiseSIMD)
target_link_libraries(dynamical PRIVATE GameNetworkingSockets_s)


# Copy resources to build
file(
    COPY resources/
    DESTINATION ${PROJECT_BINARY_DIR}/resources/
)

add_custom_command(TARGET dynamical POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_BINARY_DIR}/resources"
)


### PREPROCESS RAYCAST DATA
add_subdirectory(extra/raycast)

# Copy data to resources
add_custom_target(raycast_copy ALL
   DEPENDS raycast_run
)

add_custom_command(TARGET raycast_copy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/extra/raycast/raycast_data.bin" "${PROJECT_BINARY_DIR}/resources/raycast_data.bin"
)

add_dependencies(dynamical raycast_copy)

### COMPILE SHADERS
set(GLSL_VALIDATOR "glslangValidator")

file(GLOB_RECURSE GLSL_SOURCE_FILES "src/renderer/shaders/*.glsl")

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/resources/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/resources/shaders/"
    COMMAND ${GLSL_VALIDATOR} -V --target-env vulkan1.1 ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

add_dependencies(dynamical shaders)
