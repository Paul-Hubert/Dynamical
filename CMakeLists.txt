cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD 17)

project(dynamical)

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE)
add_subdirectory(lib/FastNoise2)

add_subdirectory(lib/VulkanMemoryAllocator)

include_directories("./src")
include_directories("./lib")

file(GLOB_RECURSE sources ./src/main.cpp ./src/*.cpp ./src/*.h)

add_executable(dynamical ${sources})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ./)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -g")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(dynamical PRIVATE -Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -Wno-class-memaccess -Wno-unused-but-set-variable -Wno-reorder -Wno-missing-field-initializers -Wno-unused-function -Wno-type-limits)
endif()

find_package(Vulkan REQUIRED)
target_link_libraries(dynamical PRIVATE Vulkan::Vulkan)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})
target_link_libraries(dynamical PRIVATE ${SDL2_LIBRARIES})

find_path(CEREAL_INCLUDE_DIRS "cereal")
target_include_directories(dynamical PRIVATE ${CEREAL_INCLUDE_DIRS})

find_path(GLM_INCLUDE_DIR "glm")
target_include_directories(dynamical PRIVATE ${GLM_INCLUDE_DIRS})

find_path(TASKFLOW_INCLUDE_DIR "taskflow")
target_include_directories(dynamical PRIVATE ${TASKFLOW_INCLUDE_DIRS})

target_include_directories(dynamical PRIVATE "lib/imgui")
target_sources(
  dynamical PRIVATE	 
  "lib/imgui/imgui.cpp"
  "lib/imgui/imgui_demo.cpp"
  "lib/imgui/imgui_draw.cpp"
  "lib/imgui/imgui_tables.cpp"
  "lib/imgui/imgui_widgets.cpp"
) 

target_link_libraries(dynamical PRIVATE FastNoise)
target_include_directories(dynamical PRIVATE "lib/FastNoise2/include/")

target_link_libraries(dynamical PRIVATE VulkanMemoryAllocator)
target_include_directories(dynamical PRIVATE "lib/VulkanMemoryAllocator/include/")

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
