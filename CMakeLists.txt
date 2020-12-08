cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD 17)

project(dynamical)

include_directories("./src")
include_directories("./3rdparty")

file(GLOB_RECURSE sources ./src/main.cpp ./src/*.cpp ./src/*.h)

add_executable(dynamical ${sources})

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(dynamical PRIVATE -Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -Wno-class-memaccess -Wno-unused-but-set-variable -Wno-reorder -Wno-missing-field-initializers -Wno-unused-function -Wno-type-limits)
endif()


find_package(Vulkan REQUIRED)
target_link_libraries(dynamical PRIVATE Vulkan::Vulkan)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE SDL2::SDL2 SDL2::SDL2main)

find_package(imgui CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE imgui::imgui)

find_package(EnTT CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE EnTT::EnTT)

find_package(cereal CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE cereal)

find_package(glm CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE glm)

find_package(Taskflow CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE Taskflow::Taskflow)

find_path(STB_INCLUDE_DIRS "stb.h")
target_include_directories(dynamical PRIVATE ${STB_INCLUDE_DIRS})

find_package(unofficial-vulkan-memory-allocator CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE unofficial::vulkan-memory-allocator::vulkan-memory-allocator)

find_package(OpenXR CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE OpenXR::headers OpenXR::openxr_loader OpenXR::openxr-all-supported)

find_package(Bullet CONFIG REQUIRED)
target_include_directories(dynamical PUBLIC ${BULLET_INCLUDE_DIRS})
target_link_libraries(dynamical PRIVATE BulletDynamics BulletCollision LinearMath)

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
target_include_directories(dynamical PRIVATE ${TINYGLTF_INCLUDE_DIRS})

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE nlohmann_json nlohmann_json::nlohmann_json)

target_compile_definitions(dynamical PRIVATE OPTICK_ENABLE_GPU_D3D12=0)
target_compile_definitions(dynamical PRIVATE OPTICK_ENABLE_GPU_VULKAN=1)

# Copy resources to build
file(
    COPY resources/
    DESTINATION ${PROJECT_BINARY_DIR}/resources/
)

add_custom_command(TARGET dynamical POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_BINARY_DIR}/resources"
)

### COMPILE SHADERS
set(GLSL_VALIDATOR "glslangValidator")

file(GLOB_RECURSE GLSL_SOURCE_FILES "src/renderer/shaders/*.glsl")

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/resources/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/resources/shaders/"
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

add_dependencies(dynamical shaders)
