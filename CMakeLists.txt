cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)

project(dynamical)

# Test names with spaces and punctuation (the test IDs of phases.md)
cmake_policy(SET CMP0110 NEW)

option(DYNAMICAL_BUILD_VIEWER "Build dynamical (Vulkan, SDL, OpenXR, shaders)" ON)   # OFF in CI
option(DYNAMICAL_TRAIN "Build the headless training executable" ON)
option(DYNAMICAL_TESTS "Build the tests" ON)
option(DYNAMICAL_BUILD_TOOLS "Build offline tools (grass raycast baker)" OFF)
set(DYNAMICAL_TRAIN_NAME "TrainingEnv" CACHE STRING "Output name of dynamical_train")

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE)
add_subdirectory(lib/FastNoise2)

# Common flags for every target below (master set them on dynamical only)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    add_compile_options(/bigobj /utf-8)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -Wno-class-memaccess -Wno-unused-but-set-variable -Wno-reorder -Wno-missing-field-initializers -Wno-unused-function -Wno-type-limits)
endif()

find_package(EnTT CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_path(CEREAL_INCLUDE_DIRS "cereal")
find_path(STB_INCLUDE_DIRS "stb_image.h")

### SIMULATION LIBRARY: no Vulkan, SDL, OpenXR or ImGui (checked by the T1.1 header-boundary test)
file(GLOB_RECURSE SIM_SOURCES CONFIGURE_DEPENDS ./src/logic/*.cpp ./src/physics/*.cpp ./src/scene/*.cpp ./src/util/*.cpp)
add_library(dynamical_sim STATIC ${SIM_SOURCES})
target_include_directories(dynamical_sim PUBLIC src lib/FastNoise2/include ${CEREAL_INCLUDE_DIRS})
target_link_libraries(dynamical_sim PUBLIC EnTT::EnTT Jolt::Jolt glm::glm nlohmann_json::nlohmann_json Taskflow::Taskflow FastNoise)

### VIEWER
if (DYNAMICAL_BUILD_VIEWER)

add_subdirectory(lib/VulkanMemoryAllocator)

file(GLOB_RECURSE VIEW_SOURCES CONFIGURE_DEPENDS ./src/main.cpp ./src/view/*.cpp ./src/view/*.h ./src/renderer/*.cpp ./src/renderer/*.h ./src/extra/*.cpp ./src/extra/*.h)

add_executable(dynamical ${VIEW_SOURCES})

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

target_include_directories(dynamical PRIVATE lib lib/imgui lib/implot lib/VulkanMemoryAllocator/include ${SDL2_INCLUDE_DIR} ${STB_INCLUDE_DIRS})
target_link_libraries(dynamical PRIVATE dynamical_sim Vulkan::Vulkan ${SDL2_LIBRARIES} OpenXR::headers OpenXR::openxr_loader VulkanMemoryAllocator)

target_sources(
  dynamical PRIVATE
  "lib/imgui/imgui.cpp"
  "lib/imgui/imgui_demo.cpp"
  "lib/imgui/imgui_draw.cpp"
  "lib/imgui/imgui_tables.cpp"
  "lib/imgui/imgui_widgets.cpp"
  "lib/implot/implot.cpp"
  "lib/implot/implot_demo.cpp"
  "lib/implot/implot_items.cpp"
)

# Copy resources to build
file(
    COPY resources/
    DESTINATION ${PROJECT_BINARY_DIR}/resources/
)

add_custom_command(TARGET dynamical POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_BINARY_DIR}/resources"
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
    # vulkan1.1 is needed for the subgroup operations in the marching cubes shader
    COMMAND ${GLSL_VALIDATOR} -V --target-env vulkan1.1 ${GLSL} -o ${SPIRV} -g
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

add_dependencies(dynamical shaders)

endif()

### HEADLESS TRAINING EXECUTABLE (TrainingEnv.exe: the trainer globs <path>.exe)
if (DYNAMICAL_TRAIN)
    file(GLOB_RECURSE TRAIN_SOURCES CONFIGURE_DEPENDS ./src/train/*.cpp)
    add_executable(dynamical_train src/train_main.cpp ${TRAIN_SOURCES})
    target_link_libraries(dynamical_train PRIVATE dynamical_sim)
    set_target_properties(dynamical_train PROPERTIES OUTPUT_NAME ${DYNAMICAL_TRAIN_NAME})

    # Deployed layout: the executable with resources/{scenes,prefabs,agents} next to it
    foreach(dir scenes prefabs agents)
        if (EXISTS ${CMAKE_SOURCE_DIR}/resources/${dir})
            add_custom_command(TARGET dynamical_train POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/${dir}" "$<TARGET_FILE_DIR:dynamical_train>/resources/${dir}")
        endif()
    endforeach()
endif()

### IMAGE COMPARISON (visual tests)
add_library(imgdiff_core STATIC tools/imgdiff/imgdiff.cpp tools/imgdiff/stb_impl.cpp)
target_include_directories(imgdiff_core PUBLIC tools ${STB_INCLUDE_DIRS})
add_executable(imgdiff tools/imgdiff/main.cpp)
target_link_libraries(imgdiff PRIVATE imgdiff_core)

### TESTS
if (DYNAMICAL_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    include(${doctest_DIR}/doctest.cmake)

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS ./tests/*.cpp)
    add_executable(dynamical_tests ${test_sources})
    target_include_directories(dynamical_tests PRIVATE tests)
    target_link_libraries(dynamical_tests PRIVATE dynamical_sim doctest::doctest imgdiff_core)

    # doctest test suites (unit, scenario, protocol) become CTest labels
    doctest_discover_tests(dynamical_tests ADD_LABELS 1 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    if (DYNAMICAL_TRAIN AND MSVC)
        get_filename_component(MSVC_BIN "${CMAKE_LINKER}" DIRECTORY)
        find_program(DUMPBIN dumpbin HINTS "${MSVC_BIN}")
        add_test(NAME "T1.2 dynamical_train imports no vulkan-1, SDL2 or openxr_loader DLL"
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:dynamical_train> -DDUMPBIN=${DUMPBIN} -P ${CMAKE_SOURCE_DIR}/tests/import_table.cmake)
        set_tests_properties("T1.2 dynamical_train imports no vulkan-1, SDL2 or openxr_loader DLL" PROPERTIES LABELS unit)
    endif()

    # Visual and probe tests run dynamical: they need a GPU (label gpu, not run in CI)
    if (DYNAMICAL_BUILD_VIEWER)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(VISUAL_TEST ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/visual_test.py)
        set(VISUAL_EXES --exe $<TARGET_FILE:dynamical> --imgdiff $<TARGET_FILE:imgdiff>)
        set(ARTIFACTS ${CMAKE_BINARY_DIR}/artifacts)
        set(CAPTURES ${CMAKE_SOURCE_DIR}/tests/capture)

        add_test(NAME "V0.1 base captures are deterministic"
            COMMAND ${VISUAL_TEST} repeat ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P0/repeat --mae 0.1)
        add_test(NAME "V0.2 base captures match the goldens"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P0/captures)
        add_test(NAME "P0 check: imgdiff fails on a capture with the camera moved by 0.1 m"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base_moved.capture.json --out ${ARTIFACTS}/P0/moved --expect-fail)
        add_test(NAME "S0.1 base probe is deterministic"
            COMMAND ${VISUAL_TEST} probe --exe $<TARGET_FILE:dynamical> --out ${ARTIFACTS}/P0/probe --runs 2 --tol 1e-6
                -- probe_entities=RigidBody probe_every=60 ticks=900)
        set_tests_properties(
            "V0.1 base captures are deterministic"
            "V0.2 base captures match the goldens"
            "P0 check: imgdiff fails on a capture with the camera moved by 0.1 m"
            "S0.1 base probe is deterministic"
            PROPERTIES LABELS gpu RUN_SERIAL TRUE WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
endif()

### TOOLS
if (DYNAMICAL_BUILD_TOOLS)
    # Bakes resources/raycast_data.bin, the lookup texture used by the grass shader
    add_executable(raycast tools/raycast/main.cpp)
    target_include_directories(raycast PRIVATE "./src" ${CEREAL_INCLUDE_DIRS})
    target_link_libraries(raycast PRIVATE glm::glm)
endif()
